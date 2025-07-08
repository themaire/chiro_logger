#include "ble_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <esp_gatt_common_api.h>
#include <esp_bt_defs.h>

static const char *TAG = "BLE_MANAGER";

/**
 * 🔧 Convertir un UUID au format string vers le format binaire little-endian
 * UUID format: "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"
 * 
 * @param uuid_str UUID au format string (ex: "12345678-1234-1234-1234-123456789ABC")
 * @param uuid_bin Buffer de sortie 16 bytes pour l'UUID binaire
 */
static void uuid_string_to_bin(const char *uuid_str, uint8_t *uuid_bin) {
    // Convertir l'UUID string en bytes - BLE utilise little-endian
    // Format: "12345678-1234-1234-1234-123456789ABC"
    //          0        1    2    3    4
    //          01234567890123456789012345678901234567
    
    // Inverser l'ordre des bytes pour le format BLE little-endian
    // Partie 1: 12345678 (4 bytes) -> positions 12-15 (reversed)
    sscanf(&uuid_str[0], "%2hhx", &uuid_bin[15]);
    sscanf(&uuid_str[2], "%2hhx", &uuid_bin[14]);
    sscanf(&uuid_str[4], "%2hhx", &uuid_bin[13]);
    sscanf(&uuid_str[6], "%2hhx", &uuid_bin[12]);
    
    // Partie 2: 1234 (2 bytes) -> positions 10-11 (reversed)
    sscanf(&uuid_str[9], "%2hhx", &uuid_bin[11]);
    sscanf(&uuid_str[11], "%2hhx", &uuid_bin[10]);
    
    // Partie 3: 1234 (2 bytes) -> positions 8-9 (reversed)
    sscanf(&uuid_str[14], "%2hhx", &uuid_bin[9]);
    sscanf(&uuid_str[16], "%2hhx", &uuid_bin[8]);
    
    // Partie 4: 1234 (2 bytes) -> positions 6-7 (as is)
    sscanf(&uuid_str[19], "%2hhx", &uuid_bin[7]);
    sscanf(&uuid_str[21], "%2hhx", &uuid_bin[6]);
    
    // Partie 5: 123456789ABC (6 bytes) -> positions 0-5 (reversed)
    sscanf(&uuid_str[24], "%2hhx", &uuid_bin[5]);
    sscanf(&uuid_str[26], "%2hhx", &uuid_bin[4]);
    sscanf(&uuid_str[28], "%2hhx", &uuid_bin[3]);
    sscanf(&uuid_str[30], "%2hhx", &uuid_bin[2]);
    sscanf(&uuid_str[32], "%2hhx", &uuid_bin[1]);
    sscanf(&uuid_str[34], "%2hhx", &uuid_bin[0]);
}

// Variables globales du module
static ble_state_t current_state = BLE_STATE_STOPPED;
static ble_transfer_stats_t transfer_stats = {0};
static int64_t transfer_start_time = 0;
static uint16_t gatts_if = ESP_GATT_IF_NONE;
static uint16_t conn_id = 0;
static uint16_t char_data_handle = 0;
// TODO: Implémenter le service de statut BLE
// static uint16_t char_status_handle = 0;

// UUID du service à annoncer (format binaire pour la publicité)
// UUID: "12345678-1234-1234-1234-123456789ABC" 
// Sera initialisé dynamiquement dans ble_manager_init()
static uint8_t service_uuid_128[16];

// Configuration de la publicité BLE
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006, // Intervalle publicitaire rapide pour découverte
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 16,  // UUID 128 bits
    .p_service_uuid = service_uuid_128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// TODO: Profil GATT pour les services - implémentation future
// Sera utilisé pour créer le service BLE de transfert de données
// 
// 🔧 QUAND RÉACTIVER LE GATT :
// 1. Décommenter la structure gatt_db[] ci-dessous
// 2. Décommenter la variable char_status_handle
// 3. Ajouter esp_ble_gatts_create_attr_tab() dans ble_manager_init()
// 4. Implémenter la gestion des handles dans gatts_event_handler()
// 
// 🎯 AVANTAGES DU GATT :
// - Services structurés pour la PWA
// - Notifications automatiques
// - Contrôle fin du transfert
// - Compatibilité avec les outils BLE standards
/* 
static esp_gatts_attr_db_t gatt_db[4] = {
    // Service déclaration
    [0] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_PRI_SERVICE}, ESP_GATT_PERM_READ,
         sizeof(uint16_t), sizeof(uint16_t), (uint8_t *)&(uint16_t){0x180F}}
    },
    
    // Caractéristique de données - déclaration
    [1] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&(uint16_t){ESP_GATT_UUID_CHAR_DECLARE}, ESP_GATT_PERM_READ,
         1, 1, (uint8_t *)&(uint8_t){ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY}}
    },
    
    // Caractéristique de données - valeur
    [2] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)BLE_CHAR_DATA_UUID, ESP_GATT_PERM_READ,
         512, 0, NULL}
    },
    
    // Caractéristique de statut - valeur
    [3] = {
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)BLE_CHAR_STATUS_UUID, ESP_GATT_PERM_READ,
         64, 0, NULL}
    }
};
*/

/**
 * 🔧 Callback pour les événements GAP BLE
 */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "📡 Données de publicité configurées");
            esp_ble_gap_start_advertising(&adv_params);
            break;
            
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "🚀 Publicité BLE démarrée - En attente de connexion...");
                current_state = BLE_STATE_ADVERTISING;
            } else {
                ESP_LOGE(TAG, "❌ Erreur démarrage publicité BLE");
            }
            break;
            
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            ESP_LOGI(TAG, "⏹️ Publicité BLE arrêtée");
            break;
            
        default:
            ESP_LOGD(TAG, "🔧 Événement GAP non géré: %d", event);
            break;
    }
}

/**
 * 🔧 Callback pour les événements GATTS
 */
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if_param, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(TAG, "🔧 Service GATT enregistré");
            gatts_if = gatts_if_param;
            
            // Créer le service BLE avec l'UUID défini
            esp_gatt_srvc_id_t service_id;
            service_id.is_primary = true;
            service_id.id.inst_id = 0;
            service_id.id.uuid.len = ESP_UUID_LEN_128;
            memcpy(service_id.id.uuid.uuid.uuid128, service_uuid_128, 16);
            
            esp_err_t create_ret = esp_ble_gatts_create_service(gatts_if, &service_id, 4);
            if (create_ret != ESP_OK) {
                ESP_LOGE(TAG, "❌ Erreur création service: %s", esp_err_to_name(create_ret));
            }
            
            // Configurer le nom du device
            esp_ble_gap_set_device_name(BLE_DEVICE_NAME);
            esp_ble_gap_config_adv_data(&adv_data);
            break;
            
        case ESP_GATTS_CREATE_EVT:
            ESP_LOGI(TAG, "🔧 Service créé avec handle %d", param->create.service_handle);
            
            // Ajouter une caractéristique de données
            esp_bt_uuid_t char_uuid;
            char_uuid.len = ESP_UUID_LEN_128;
            // Convertir l'UUID de données depuis le header
            uint8_t char_data_uuid[16];
            uuid_string_to_bin(BLE_CHAR_DATA_UUID, char_data_uuid);
            memcpy(char_uuid.uuid.uuid128, char_data_uuid, 16);
            
            esp_err_t add_char_ret = esp_ble_gatts_add_char(param->create.service_handle,
                                                           &char_uuid,
                                                           ESP_GATT_PERM_READ,
                                                           ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                                           NULL, NULL);
            if (add_char_ret != ESP_OK) {
                ESP_LOGE(TAG, "❌ Erreur ajout caractéristique: %s", esp_err_to_name(add_char_ret));
            }
            
            // Démarrer le service
            esp_err_t start_ret = esp_ble_gatts_start_service(param->create.service_handle);
            if (start_ret != ESP_OK) {
                ESP_LOGE(TAG, "❌ Erreur démarrage service: %s", esp_err_to_name(start_ret));
            }
            break;
            
        case ESP_GATTS_ADD_CHAR_EVT:
            ESP_LOGI(TAG, "🔧 Caractéristique ajoutée avec handle %d", param->add_char.attr_handle);
            char_data_handle = param->add_char.attr_handle;
            break;
            
        case ESP_GATTS_START_EVT:
            ESP_LOGI(TAG, "🚀 Service démarré avec handle %d", param->start.service_handle);
            break;
            
        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "📱 Client connecté!");
            current_state = BLE_STATE_CONNECTED;
            conn_id = param->connect.conn_id;
            transfer_start_time = esp_timer_get_time();
            
            // Arrêter la publicité
            esp_ble_gap_stop_advertising();
            break;
            
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "📱 Client déconnecté");
            current_state = BLE_STATE_STOPPED;
            
            // Calculer les statistiques
            if (transfer_start_time > 0) {
                transfer_stats.connection_time = (esp_timer_get_time() - transfer_start_time) / 1000000;
            }
            break;
            
        case ESP_GATTS_READ_EVT:
            ESP_LOGI(TAG, "📖 Lecture demandée sur handle %d", param->read.handle);
            // La PWA lit les données - déclencher l'envoi
            if (param->read.handle == char_data_handle) {
                ble_manager_send_data();
            }
            break;
            
        default:
            ESP_LOGD(TAG, "🔧 Événement GATTS non géré: %d", event);
            break;
    }
}

/**
 * 🚀 Initialiser le module BLE
 */
esp_err_t ble_manager_init(void)
{
    ESP_LOGI(TAG, "🔧 Initialisation du module BLE...");
    
    // Initialiser les UUIDs à partir des chaînes de caractères
    uuid_string_to_bin(BLE_SERVICE_UUID, service_uuid_128);
    
    // Debug : afficher l'UUID du service en hexadécimal
    ESP_LOGI(TAG, "🔧 UUID Service: %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             service_uuid_128[15], service_uuid_128[14], service_uuid_128[13], service_uuid_128[12],
             service_uuid_128[11], service_uuid_128[10], service_uuid_128[9], service_uuid_128[8],
             service_uuid_128[7], service_uuid_128[6], service_uuid_128[5], service_uuid_128[4],
             service_uuid_128[3], service_uuid_128[2], service_uuid_128[1], service_uuid_128[0]);
    
    // Initialiser le contrôleur Bluetooth
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Erreur init contrôleur BT: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Activer le contrôleur en mode BLE uniquement
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Erreur activation contrôleur BLE: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialiser Bluedroid
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Erreur init Bluedroid: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Erreur activation Bluedroid: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Enregistrer les callbacks
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Erreur enregistrement callback GAP: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Erreur enregistrement callback GATTS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Enregistrer l'application GATT
    ret = esp_ble_gatts_app_register(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Erreur enregistrement app GATT: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ Module BLE initialisé");
    return ESP_OK;
}

/**
 * 📡 Démarrer le mode transfert BLE
 */
esp_err_t ble_manager_start_transfer_mode(void)
{
    ESP_LOGI(TAG, "🚀 Démarrage du mode transfert BLE...");
    
    if (current_state == BLE_STATE_CONNECTED || current_state == BLE_STATE_TRANSFERRING) {
        ESP_LOGW(TAG, "⚠️ Transfert déjà en cours");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Si BLE est déjà en publicité, c'est normal après l'init
    if (current_state == BLE_STATE_ADVERTISING) {
        ESP_LOGI(TAG, "📡 Mode transfert activé - Recherche de l'application PWA...");
        return ESP_OK;
    }
    
    // Si BLE est arrêté, le redémarrer
    if (current_state == BLE_STATE_STOPPED) {
        // Réinitialiser les statistiques
        memset(&transfer_stats, 0, sizeof(transfer_stats));
        transfer_start_time = esp_timer_get_time();
        
        // Démarrer la publicité (sera gérée par le callback)
        esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ Erreur configuration publicité: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    ESP_LOGI(TAG, "📡 Mode transfert activé - Recherche de l'application PWA...");
    return ESP_OK;
}

/**
 * ⏹️ Arrêter le BLE et retourner en mode normal
 */
esp_err_t ble_manager_stop(void)
{
    ESP_LOGI(TAG, "⏹️ Arrêt du module BLE...");
    
    if (current_state == BLE_STATE_STOPPED) {
        return ESP_OK;
    }
    
    // Arrêter la publicité
    esp_ble_gap_stop_advertising();
    
    // Déconnecter si nécessaire
    if (current_state == BLE_STATE_CONNECTED && conn_id != 0) {
        esp_ble_gatts_close(gatts_if, conn_id);
    }
    
    current_state = BLE_STATE_STOPPED;
    ESP_LOGI(TAG, "✅ BLE arrêté - Retour au mode normal");
    
    return ESP_OK;
}

/**
 * 📊 Obtenir l'état actuel du BLE
 */
ble_state_t ble_manager_get_state(void)
{
    return current_state;
}

/**
 * 📈 Obtenir les statistiques du dernier transfert
 */
esp_err_t ble_manager_get_transfer_stats(ble_transfer_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(stats, &transfer_stats, sizeof(ble_transfer_stats_t));
    return ESP_OK;
}

/**
 * 📤 Envoyer les données CSV via BLE
 */
esp_err_t ble_manager_send_data(void)
{
    ESP_LOGI(TAG, "📤 Début du transfert de données...");
    
    if (current_state != BLE_STATE_CONNECTED) {
        ESP_LOGE(TAG, "❌ Pas de client connecté pour le transfert");
        return ESP_ERR_INVALID_STATE;
    }
    
    current_state = BLE_STATE_TRANSFERRING;
    
    // TODO: Implémenter la lecture et l'envoi des données CSV
    // Pour l'instant, envoyer des données de test
    const char *test_data = "ID,DateTime,Temperature_C,Humidity_%\n1,1672531200,18.5,85.0\n2,1672531205,18.6,85.2\n";
    
    esp_err_t ret = esp_ble_gatts_send_indicate(gatts_if, conn_id, char_data_handle,
                                                strlen(test_data), (uint8_t*)test_data, false);
    
    if (ret == ESP_OK) {
        transfer_stats.bytes_sent += strlen(test_data);
        transfer_stats.records_sent += 2; // 2 mesures de test
        ESP_LOGI(TAG, "✅ Données envoyées: %d octets", strlen(test_data));
    } else {
        ESP_LOGE(TAG, "❌ Erreur envoi données: %s", esp_err_to_name(ret));
    }
    
    current_state = BLE_STATE_CONNECTED;
    return ret;
}

/**
 * ⏱️ Vérifier le timeout du mode transfert
 */
bool ble_manager_check_timeout(void)
{
    if (current_state == BLE_STATE_STOPPED || transfer_start_time == 0) {
        return false;
    }
    
    int64_t elapsed = (esp_timer_get_time() - transfer_start_time) / 1000000;
    
    if (elapsed > BLE_TRANSFER_TIMEOUT_SEC) {
        ESP_LOGW(TAG, "⏱️ Timeout du mode transfert (%lld sec)", elapsed);
        ble_manager_stop();
        return true;
    }
    
    return false;
}
