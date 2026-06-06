/**
 * @file ble_manager.c
 * @brief Gestion BLE NimBLE pour le Chiro Logger (ESP32-C3 / ESP-IDF 5.x)
 *
 * Stack : NimBLE (recommandé pour ESP32-C3 RISC-V)
 *
 * Architecture :
 *   - Un service GATT primaire (UUID 128-bit)
 *   - Caractéristique DATA  : READ + NOTIFY → transfert CSV vers la PWA
 *   - Caractéristique STATUS: READ + WRITE  → commandes depuis la PWA
 *
 * Flux de transfert :
 *   1. ESP32 démarre la publicité (advertising) avec l'UUID de service inclus
 *   2. La PWA Angular se connecte et appelle startNotifications() sur DATA
 *   3. La PWA appelle readValue() → déclenche le callback GATT READ côté ESP32
 *   4. L'ESP32 envoie les lignes CSV une par une via NOTIFY
 *   5. La PWA reçoit chaque ligne dans 'characteristicvaluechanged'
 *   6. L'ESP32 termine avec "###EOF###\n"
 */

#include "ble_manager.h"

/* Fichier CSV source : carte SD (monté par main avant le transfert BLE) */
#define SD_CSV_FILE  "/sdcard/CHIRO/data.csv"

/* Nombre maximum de lignes à envoyer (les N dernières du fichier SD) */
#define BLE_MAX_LINES 2000

#include <string.h>
#include <stdio.h>

/* NimBLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* ESP-IDF */
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "BLE_MGR";

/* ── UUIDs (format little-endian pour NimBLE) ──────────────────────────────
 *
 * Service  : 12345678-1234-1234-1234-123456789ABC
 * DATA     : 87654321-4321-4321-4321-CBA987654321  (READ + NOTIFY)
 * STATUS   : 11111111-2222-3333-4444-555555555555  (READ + WRITE)
 *
 * Conversion UUID string (big-endian) → tableau little-endian (index 0 = LSB).
 */
static const ble_uuid128_t ble_svc_uuid = BLE_UUID128_INIT(
    0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12,
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
);

static const ble_uuid128_t ble_chr_data_uuid = BLE_UUID128_INIT(
    0x21, 0x43, 0x65, 0x87, 0xA9, 0xCB, 0x21, 0x43,
    0x21, 0x43, 0x21, 0x43, 0x21, 0x43, 0x65, 0x87
);

static const ble_uuid128_t ble_chr_status_uuid = BLE_UUID128_INIT(
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x44, 0x44,
    0x33, 0x33, 0x22, 0x22, 0x11, 0x11, 0x11, 0x11
);

/* ── État global ─────────────────────────────────────────────────────────── */
static ble_state_t       g_state           = BLE_STATE_STOPPED;
static uint16_t          g_conn_handle     = BLE_HS_CONN_HANDLE_NONE;
static uint16_t          g_data_val_handle = 0;
static uint8_t           g_own_addr_type   = 0;
static TaskHandle_t      g_sender_task     = NULL;
static SemaphoreHandle_t g_send_sem        = NULL;

/* ── Prototypes internes ─────────────────────────────────────────────────── */
static int  gatt_data_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int  gatt_status_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);
static int  gap_event_handler(struct ble_gap_event *event, void *arg);
static void start_advertising(void);
static void ble_host_task(void *param);
static void data_sender_task(void *param);
static void on_sync(void);
static void on_reset(int reason);

/* ── Table de services GATT ──────────────────────────────────────────────── */
static const struct ble_gatt_svc_def g_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &ble_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                /* DATA : READ déclenche l'envoi, NOTIFY transmet les lignes CSV */
                .uuid       = &ble_chr_data_uuid.u,
                .access_cb  = gatt_data_access_cb,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_data_val_handle,
            },
            {
                /* STATUS : commandes simples depuis la PWA */
                .uuid      = &ble_chr_status_uuid.u,
                .access_cb = gatt_status_access_cb,
                .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            { 0 }, /* marqueur de fin */
        },
    },
    { 0 }, /* marqueur de fin */
};

/* ── Callbacks GATT ──────────────────────────────────────────────────────── */

/**
 * Callback DATA : la PWA lit la caractéristique → déclenche l'envoi.
 * On répond immédiatement "DATA_READY" et on envoie les CSV en NOTIFY
 * depuis la tâche dédiée pour ne pas bloquer le stack BLE.
 */
static int gatt_data_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        ESP_LOGI(TAG, "📖 READ DATA — lancement de l'envoi CSV");
        if (g_send_sem != NULL) {
            xSemaphoreGiveFromISR(g_send_sem, NULL);
        }
        const char *ack = "DATA_READY";
        return os_mbuf_append(ctxt->om, ack, strlen(ack)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * Callback STATUS : commandes de la PWA (BATTERY_STATUS, CLEAR_DATA…).
 */
static int gatt_status_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char *s = "READY";
        return os_mbuf_append(ctxt->om, s, strlen(s)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char cmd[65] = {0};
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 64) len = 64;
        os_mbuf_copydata(ctxt->om, 0, len, cmd);
        ESP_LOGI(TAG, "📥 Commande STATUS : %s", cmd);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* ── Helper : envoi NOTIFY compatible MTU variable ───────────────────────── */

/**
 * Envoie 'len' octets de 'data' en NOTIFY en respectant le MTU négocié.
 * Découpe automatiquement en chunks si la donnée dépasse le payload max.
 * Compatible BT 4.2 (MTU 23 → payload 20) et BT 5.x (MTU jusqu'à 517).
 * Angular reconstitue les lignes côté client grâce au buffering sur '\n'.
 *
 * @param had_retry  Mis à true si au moins un retry ENOMEM a eu lieu (sortie).
 *                   Permet à l'appelant d'adapter le délai inter-lignes.
 * @return 0 si succès, code d'erreur NimBLE sinon.
 */
static int notify_chunked(const char *data, size_t len, bool *had_retry)
{
    uint16_t mtu = ble_att_mtu(g_conn_handle);
    if (mtu < 4) mtu = 23;
    uint16_t max_payload = mtu - 3;

    const char *ptr       = data;
    size_t      remaining = len;
    int         rc        = 0;
    if (had_retry) *had_retry = false;

    while (remaining > 0) {
        uint16_t chunk = (remaining > max_payload)
                         ? max_payload : (uint16_t)remaining;

        /*
         * Retry sur BLE_HS_ENOMEM (rc=6) : pool os_mbuf saturée.
         * Délai de 30 ms = un connection interval BT 4.2 (≥ 30 ms typique).
         * NimBLE libère l'om passé à ble_gatts_notify_custom() même en cas
         * d'erreur — pas besoin de os_mbuf_free_chain() manuellement.
         */
        int retries = 0;
        do {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(ptr, chunk);
            if (!om) {
                vTaskDelay(pdMS_TO_TICKS(30));
                rc = BLE_HS_ENOMEM;
                retries++;
                continue;
            }
            rc = ble_gatts_notify_custom(g_conn_handle, g_data_val_handle, om);
            if (rc == BLE_HS_ENOMEM) {
                vTaskDelay(pdMS_TO_TICKS(30));
                retries++;
            }
        } while (rc == BLE_HS_ENOMEM && retries < 8);

        if (retries > 0 && had_retry) *had_retry = true;
        if (rc != 0) return rc;
        ptr       += chunk;
        remaining -= chunk;
        if (remaining > 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    return rc;
}

/* ── Tâche d'envoi des données ───────────────────────────────────────────── */

/**
 * Tâche FreeRTOS dédiée à l'envoi des lignes CSV en NOTIFY.
 * Attend le sémaphore donné par le callback READ (ou ble_manager_send_data).
 */
static void data_sender_task(void *param)
{
    (void)param;

    for (;;) {
        /* Attendre le signal d'envoi */
        if (xSemaphoreTake(g_send_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGW(TAG, "⚠️  Envoi annulé : pas de connexion active");
            continue;
        }

        g_state = BLE_STATE_TRANSFERRING;
        uint16_t mtu = ble_att_mtu(g_conn_handle);
        ESP_LOGI(TAG, "📤 Début du transfert CSV depuis SD (MTU=%d, payload=%d)...",
                 mtu, mtu - 3);

        /* Lire depuis la carte SD (montée par main.c avant handle_transfer_mode) */
        FILE *f = fopen(SD_CSV_FILE, "r");

        if (f == NULL) {
            notify_chunked("NO_DATA\n", 8, NULL);
            ESP_LOGW(TAG, "⚠️  Fichier SD introuvable : %s", SD_CSV_FILE);
        } else {
            /* --- Passe 1 : compter les lignes de données (hors en-tête) --- */
            char line[320];
            int  total_data_lines = 0;
            bool skip_hdr = true;
            while (fgets(line, sizeof(line), f)) {
                if (skip_hdr) { skip_hdr = false; continue; }
                if (strlen(line) > 1) total_data_lines++;
            }
            rewind(f);

            int send_count = (total_data_lines > BLE_MAX_LINES)
                             ? BLE_MAX_LINES : total_data_lines;
            int skip_count = total_data_lines - send_count;
            ESP_LOGI(TAG, "📊 %d lignes totales → envoi des %d dernières",
                     total_data_lines, send_count);

            /* Paquet de métadonnées — doit arriver AVANT le header CSV */
            char meta[48];
            snprintf(meta, sizeof(meta), "###META:lines=%d###\n", send_count);
            notify_chunked(meta, strlen(meta), NULL);
            vTaskDelay(pdMS_TO_TICKS(5));

            /* En-tête CSV */
            const char *hdr =
                "ID,DateTime,Temperature_C,Humidity_%,Battery_%,Battery_V\n";
            notify_chunked(hdr, strlen(hdr), NULL);
            vTaskDelay(pdMS_TO_TICKS(5));

            /* --- Passe 2 : sauter les anciennes lignes, envoyer les dernières --- */
            skip_hdr     = true;
            int      skipped = 0;
            uint32_t sent    = 0;

            while (fgets(line, sizeof(line), f)) {
                /* Ignorer l'en-tête du fichier SD */
                if (skip_hdr) { skip_hdr = false; continue; }
                /* Sauter les lignes hors fenêtre */
                if (skipped < skip_count) { skipped++; continue; }

                size_t len = strlen(line);
                if (len <= 1) continue;

                bool had_retry = false;
                int rc = notify_chunked(line, len, &had_retry);
                if (rc != 0) {
                    ESP_LOGW(TAG, "⚠️  NOTIFY erreur rc=%d, arrêt", rc);
                    break;
                }
                sent++;
                /*
                 * Délai adaptatif : 5 ms en temps normal (BT 5.x), mais si un
                 * retry ENOMEM a eu lieu la pool est encore sous pression —
                 * on attend un connection interval complet (30 ms) avant la
                 * prochaine ligne pour laisser le téléphone drainer.
                 */
                vTaskDelay(pdMS_TO_TICKS(had_retry ? 30 : 5));
            }
            fclose(f);
            ESP_LOGI(TAG, "✅ %lu lignes envoyées", (unsigned long)sent);
        }

        /* Marqueur de fin de transfert */
        notify_chunked("###EOF###\n", 10, NULL);

        g_state = BLE_STATE_CONNECTED;
        ESP_LOGI(TAG, "🏁 Transfert terminé");
    }
}

/* ── Gestion des événements GAP ──────────────────────────────────────────── */

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "📱 Client connecté (handle=%d)",
                     event->connect.conn_handle);
            g_conn_handle = event->connect.conn_handle;
            g_state       = BLE_STATE_CONNECTED;
        } else {
            ESP_LOGW(TAG, "⚠️  Connexion échouée (status=%d), reprise advertising",
                     event->connect.status);
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "📱 Client déconnecté (reason=%d)",
                 event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_state       = BLE_STATE_ADVERTISING;
        /* Reprendre la publicité pour une prochaine connexion */
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "📡 Publicité terminée");
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "🔔 NOTIFY subscribe: conn=%d cur_notify=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.cur_notify);
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "📐 MTU négocié : %d octets", event->mtu.value);
        break;

    default:
        break;
    }
    return 0;
}

/* ── Démarrage de la publicité ───────────────────────────────────────────── */

static void start_advertising(void)
{
    /* ── Paquet advertising principal (max 31 octets) ──
     * Flags (3) + Nom complet "ChiroLogger" (13) = 16 octets → OK
     * L'UUID128 (18 octets) va dans la scan response pour ne pas dépasser la limite.
     */
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    const char *name    = ble_svc_gap_device_name();
    fields.name         = (const uint8_t *)name;
    fields.name_len     = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "❌ ble_gap_adv_set_fields: rc=%d", rc);
        return;
    }

    /* ── Scan response : UUID128 du service (18 octets) ──
     * Web Bluetooth (Angular optionalServices) retrouve le service via la scan
     * response aussi bien que via le paquet principal.
     */
    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    rsp_fields.uuids128             = &ble_svc_uuid;
    rsp_fields.num_uuids128         = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "❌ ble_gap_adv_rsp_set_fields: rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
                            &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "❌ ble_gap_adv_start: rc=%d", rc);
        return;
    }

    g_state = BLE_STATE_ADVERTISING;
    ESP_LOGI(TAG, "📡 Advertising démarré — UUID service dans scan response");
}

/* ── Callbacks du host NimBLE ────────────────────────────────────────────── */

static void on_sync(void)
{
    ESP_LOGI(TAG, "🔧 NimBLE stack synchronisé");

    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "❌ ble_hs_util_ensure_addr: rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "❌ ble_hs_id_infer_auto: rc=%d", rc);
        return;
    }
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "♻️  NimBLE reset (reason=%d)", reason);
    g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    g_state       = BLE_STATE_STOPPED;
}

/* ── Tâche NimBLE host ───────────────────────────────────────────────────── */

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "🚀 NimBLE host task démarrée");
    nimble_port_run();          /* bloque jusqu'à nimble_port_stop() */
    nimble_port_freertos_deinit();
    ESP_LOGI(TAG, "⏹️  NimBLE host task terminée");
}

/* ── API publique ────────────────────────────────────────────────────────── */

esp_err_t ble_manager_init(void)
{
    ESP_LOGI(TAG, "🔧 Initialisation BLE NimBLE (ESP32-C3)...");

    /* NVS requis par NimBLE pour la persistance des liaisons */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "⚠️  NVS corrompu — effacement et réinitialisation");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ nvs_flash_init: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialiser le port NimBLE (gère en interne le contrôleur BLE en ESP-IDF 5.x) */
    nimble_port_init();

    /* Configurer le host NimBLE */
    ble_hs_cfg.reset_cb        = on_reset;
    ble_hs_cfg.sync_cb         = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_sc           = 0; /* Secure Connections désactivé */

    /* Services GAP et GATT standard */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* Nom du device BLE */
    int rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "❌ ble_svc_gap_device_name_set: rc=%d", rc);
        return ESP_FAIL;
    }

    /* Enregistrer les services GATT */
    rc = ble_gatts_count_cfg(g_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "❌ ble_gatts_count_cfg: rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(g_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "❌ ble_gatts_add_svcs: rc=%d", rc);
        return ESP_FAIL;
    }

    /* Sémaphore de déclenchement de l'envoi */
    g_send_sem = xSemaphoreCreateBinary();
    if (g_send_sem == NULL) {
        ESP_LOGE(TAG, "❌ Impossible de créer le sémaphore");
        return ESP_ERR_NO_MEM;
    }

    /* Tâche de transfert des données */
    BaseType_t xr = xTaskCreate(data_sender_task, "ble_sender",
                                 6144, NULL, 5, &g_sender_task);
    if (xr != pdPASS) {
        ESP_LOGE(TAG, "❌ xTaskCreate ble_sender échoué");
        return ESP_ERR_NO_MEM;
    }

    /* Démarrer le host NimBLE dans sa propre tâche FreeRTOS */
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "✅ BLE NimBLE initialisé avec succès");
    return ESP_OK;
}

esp_err_t ble_manager_start_transfer_mode(void)
{
    ESP_LOGI(TAG, "🚀 Mode transfert BLE actif — timeout=%ds",
             BLE_TRANSFER_TIMEOUT_SEC);

    int64_t start_us   = esp_timer_get_time();
    int64_t timeout_us = (int64_t)BLE_TRANSFER_TIMEOUT_SEC * 1000000LL;

    /* Polling toutes les 500 ms jusqu'au timeout ou arrêt manuel */
    while ((esp_timer_get_time() - start_us) < timeout_us) {
        if (g_state == BLE_STATE_STOPPED) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if ((esp_timer_get_time() - start_us) >= timeout_us) {
        ESP_LOGW(TAG, "⏱️  Timeout mode transfert atteint");
    }
    return ESP_OK;
}

esp_err_t ble_manager_stop(void)
{
    ESP_LOGI(TAG, "⏹️  Arrêt du module BLE...");

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    ble_gap_adv_stop();
    nimble_port_stop();

    if (g_sender_task != NULL) {
        vTaskDelete(g_sender_task);
        g_sender_task = NULL;
    }
    if (g_send_sem != NULL) {
        vSemaphoreDelete(g_send_sem);
        g_send_sem = NULL;
    }

    g_state = BLE_STATE_STOPPED;
    ESP_LOGI(TAG, "✅ BLE arrêté");
    return ESP_OK;
}

ble_state_t ble_manager_get_state(void)
{
    return g_state;
}

esp_err_t ble_manager_send_data(void)
{
    /* Déclenchement manuel de l'envoi (peut être appelé sans READ préalable) */
    if (g_send_sem != NULL) {
        xSemaphoreGive(g_send_sem);
    }
    return ESP_OK;
}
