#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <sys/stat.h>
#include <time.h>
#include <esp_spiffs.h>
#include <esp_vfs_semihost.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include "led_rgb.h"
#include "sd_card.h"
#include "battery.h"
#include "rtc_clock.h"
#include "sht45.h"

// !! Très important !! Contien les définitions de configuration
// C'est a dire les variables et constantes globales utilisées dans tout le projet
// comme : les pins, les durées de deep sleep, les chemins de fichiers, etc.
#include "config.h"

// #include "ble_manager.h"

static const char *TAG = "CHIRO_LOGGER";

// Variable stockée en RTC memory pour persister entre les deep sleeps
RTC_DATA_ATTR int cycle_counter = 0;

/*
 * 💡 OPTIMISATION ÉNERGÉTIQUE - LOGS & LED :
 * 
 * Les fonction pour afficher les logs ESP_LOGI/ESP_LOGE consomment de l'énergie car ils :
 * - Maintiennent l'UART actif (~10-20 mA)
 * - Prolongent le temps d'activité avant deep sleep
 * - Formatent et transmettent les chaînes
 * 
 * EN PRODUCTION / sur le terrain : Décommenter #define PRODUCTION_MODE
 * - Garde uniquement les logs essentiels (erreurs, flush, compteur)
 * - Supprime les logs de debug/verbose
 * - Économie estimée : 5-10% d'autonomie supplémentaire
 * 
 * La LED RGB WS2812 consomme :
 * - ~1 mA par LED en veille
 * - ~20 mA par couleur à pleine intensité (60 mA max pour blanc)
 * - Impact modéré mais mesurable sur batterie long terme
 * 
 * MODE TERRAIN (autonomie max) : Commenter #define VISUAL_MODE
 * MODE DEBUG/TEST : Décommenter #define VISUAL_MODE
 */

// Déclarations de fonctions
void print_wakeup_info(void);
esp_err_t init_wakeup_button(void);
void handle_transfer_mode(void);

// Macros pour logs économes en énergie
#ifdef PRODUCTION_MODE
    #define LOG_ESSENTIAL(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
    #define LOG_DEBUG(tag, format, ...) // Pas de log en production
    #define LOG_VERBOSE(tag, format, ...) // Pas de log en production
#else
    #define LOG_ESSENTIAL(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
    #define LOG_DEBUG(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
    #define LOG_VERBOSE(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#endif

// Fonction d'initialisation du tampon flash (partition SPIFFS qui 
// ne consomme pas de SD et ne s'efface pas en cas de coupure d'alimentation)
esp_err_t init_flash_buffer(void)
{
    LOG_DEBUG(TAG, "🔋 Initialisation du tampon flash énergétique...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = BUFFER_MOUNT_POINT,
        .partition_label = "data_buffer",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            LOG_ESSENTIAL(TAG, "❌ Impossible de monter la partition SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            LOG_ESSENTIAL(TAG, "❌ Partition 'data_buffer' introuvable");
        } else {
            LOG_ESSENTIAL(TAG, "❌ Erreur montage SPIFFS: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    
    LOG_DEBUG(TAG, "✅ Tampon flash monté sur %s", BUFFER_MOUNT_POINT);
    
    // Vérifier l'espace disponible
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("data_buffer", &total, &used);
    if (ret == ESP_OK) {
        LOG_DEBUG(TAG, "📊 Espace tampon: %zu Ko utilisés / %zu Ko total", 
                used / 1024, total / 1024);
    }
    
    return ESP_OK;
}

// Fonction pour ajouter une mesure dans le tampon flash avec ID unique
esp_err_t add_to_flash_buffer(int id, const char* datetime, float temperature, float humidity)
{
    LOG_DEBUG(TAG, "🔋 Ajout mesure au tampon flash...");
    
    // Vérifier si le fichier existe déjà
    FILE *file = fopen(BUFFER_CSV_FILE, "r");
    bool file_exists = (file != NULL);
    if (file != NULL) {
        fclose(file);
    }
    
    // Ouvrir le fichier en mode append
    file = fopen(BUFFER_CSV_FILE, "a");
    if (file == NULL) {
        LOG_ESSENTIAL(TAG, "❌ Impossible d'ouvrir le tampon CSV: %s", BUFFER_CSV_FILE);
        return ESP_FAIL;
    }
    
    // Si le fichier n'existait pas, écrire l'en-tête
    if (!file_exists) {
        LOG_DEBUG(TAG, "📄 Création du tampon CSV avec en-tête");
        fprintf(file, "ID,DateTime,Temperature_C,Humidity_%%\n");
    }
    
    // Écrire les données avec ID unique en première colonne
    fprintf(file, "%d,", id);
    
    if (datetime == NULL) {
        int64_t timestamp = esp_timer_get_time() / 1000000;
        fprintf(file, "%lld,", (long long)timestamp);
    } else {
        fprintf(file, "%s,", datetime);
    }
    
    if (temperature == -999.0f) {
        fprintf(file, "N/A,");
    } else {
        fprintf(file, "%.2f,", temperature);
    }
    
    if (humidity == -999.0f) {
        fprintf(file, "N/A\n");
    } else {
        fprintf(file, "%.2f\n", humidity);
    }
    
    fclose(file);
    LOG_DEBUG(TAG, "✅ Mesure ajoutée au tampon flash");
    
    // Signal LED rapide pour sauvegarde
    set_led_rgb(0, 255, 0, 50, 1, 0, false);  // Vert flash ultra rapide
    
    return ESP_OK;
}

// Fonction pour compter les lignes dans le tampon flash
// sert à décider quand faire un flush vers la SD
int count_buffer_lines(void)
{
    FILE *file = fopen(BUFFER_CSV_FILE, "r");
    if (file == NULL) {
        return 0;
    }
    
    int lines = 0;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), file)) {
        lines++;
    }
    fclose(file);
    
    // Soustraire l'en-tête
    return lines > 1 ? lines - 1 : 0;
}

// Fonction pour trouver le plus grand ID dans un fichier CSV
// Le CSV doit avoir l'ID en première colonne (format: ID,DateTime,...)
int get_max_id_from_csv(const char *filepath)
{
    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        return 0;
    }
    
    int max_id = 0;
    char line[256];
    bool first_line = true;
    
    while (fgets(line, sizeof(line), file)) {
        if (first_line) {
            first_line = false;
            continue; // Ignorer l'en-tête
        }
        
        int id = 0;
        if (sscanf(line, "%d,", &id) == 1) {
            if (id > max_id) {
                max_id = id;
            }
        }
    }
    
    fclose(file);
    return max_id;
}

// Fonction pour restaurer cycle_counter depuis les données existantes
// Appelée uniquement au démarrage frais (perte d'alimentation)
// Vérifie le tampon SPIFFS puis la carte SD pour trouver le dernier ID
void restore_cycle_counter_from_storage(void)
{
    LOG_ESSENTIAL(TAG, "🔍 Recherche du dernier ID dans les données existantes...");
    
    int max_id = 0;
    
    // 1. Vérifier le tampon SPIFFS (déjà monté à ce stade)
    int buffer_max = get_max_id_from_csv(BUFFER_CSV_FILE);
    if (buffer_max > max_id) {
        max_id = buffer_max;
        LOG_DEBUG(TAG, "📊 Max ID dans tampon flash: %d", buffer_max);
    }
    
    // 2. Vérifier la carte SD
    esp_err_t ret = init_sd_card();
    if (ret == ESP_OK) {
        int sd_max = get_max_id_from_csv("/sdcard/CHIRO/data.csv");
        if (sd_max > max_id) {
            max_id = sd_max;
            LOG_DEBUG(TAG, "📊 Max ID sur carte SD: %d", sd_max);
        }
        unmount_sd_card();
    } else {
        LOG_DEBUG(TAG, "⚠️  SD non accessible pour restauration ID");
    }
    
    if (max_id > 0) {
        cycle_counter = max_id;
        LOG_ESSENTIAL(TAG, "✅ Compteur restauré: %d (reprend à %d)", max_id, max_id + 1);
    } else {
        LOG_ESSENTIAL(TAG, "ℹ️  Aucune donnée existante, démarrage à ID 1");
    }
}

// Fonction pour transférer le tampon flash vers la carte SD
// sert à libérer de l'espace dans le tampon
esp_err_t flush_buffer_to_sd(void)
{
    ESP_LOGI(TAG, "🔄 Flush du tampon flash vers la carte SD...");
    
    // Vérifier si le tampon existe
    FILE *buffer_file = fopen(BUFFER_CSV_FILE, "r");
    if (buffer_file == NULL) {
        ESP_LOGI(TAG, "ℹ️  Aucun tampon à flusher");
        return ESP_OK;
    }
    
    // Compter les lignes pour information
    int buffer_lines = count_buffer_lines();
    ESP_LOGI(TAG, "📊 Flush de %d mesures vers la SD", buffer_lines);
    
    // Initialiser la carte SD
    esp_err_t ret = init_sd_card();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Impossible d'initialiser la SD pour le flush");
        fclose(buffer_file);
        return ret;
    }
    
    // Créer le répertoire CHIRO s'il n'existe pas (FAT32 ne le fait pas auto)
    struct stat st;
    if (stat("/sdcard/CHIRO", &st) != 0) {
        mkdir("/sdcard/CHIRO", 0775);
        ESP_LOGI(TAG, "📁 Répertoire /sdcard/CHIRO créé");
    }
    
    // Ouvrir le fichier de destination sur la SD
    FILE *sd_file = fopen("/sdcard/CHIRO/data.csv", "a");
    if (sd_file == NULL) {
        ESP_LOGE(TAG, "❌ Impossible d'ouvrir le fichier SD pour le flush");
        fclose(buffer_file);
        unmount_sd_card();
        return ESP_FAIL;
    }
    
    // Copier toutes les données du tampon vers la SD (sauf l'en-tête)
    char line[256];
    bool first_line = true;
    int lines_copied = 0;
    
    while (fgets(line, sizeof(line), buffer_file)) {
        if (first_line) {
            first_line = false;
            continue; // Ignorer l'en-tête
        }
        fputs(line, sd_file);
        lines_copied++;
    }
    
    fclose(buffer_file);
    fclose(sd_file);
    
    ESP_LOGI(TAG, "✅ %d lignes copiées vers la SD", lines_copied);
    
    // Signal LED de flush réussi
    set_led_rgb(0, 255, 0, 500, 10, 50, false);  // Vert 10 flashs rapides
    
    // Vider le tampon après transfert réussi
    if (remove(BUFFER_CSV_FILE) == 0) {
        ESP_LOGI(TAG, "🧹 Tampon flash vidé");
    } else {
        ESP_LOGW(TAG, "⚠️  Impossible de vider le tampon");
    }
    
    // Démonter la SD pour économiser l'énergie
    unmount_sd_card();
    
    return ESP_OK;
}

// Fonction de diagnostic du réveil
// sert à afficher la cause du réveil et le compteur persistant
void print_wakeup_info(void)
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            LOG_ESSENTIAL(TAG, "⏰ Réveil du deep sleep (timer) - Cycle #%d", cycle_counter + 1);
            break;
        case ESP_SLEEP_WAKEUP_GPIO:
            LOG_ESSENTIAL(TAG, "🔘 Réveil du deep sleep (bouton) - Mode transfert BLE activé!");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
            LOG_ESSENTIAL(TAG, "🚀 Démarrage initial du système - Reset du compteur");
            cycle_counter = 0; // Reset du compteur au premier démarrage
            break;
        default:
            LOG_ESSENTIAL(TAG, "🔄 Réveil pour cause inconnue (%d) - Cycle #%d", wakeup_reason, cycle_counter + 1);
            break;
    }
    
    // Afficher des informations sur la RTC memory
    LOG_DEBUG(TAG, "📊 Compteur RTC persistant: %d", cycle_counter);
}

// Fonction d'initialisation du bouton de réveil
// sert à configurer le GPIO et le mode wakeup
esp_err_t init_wakeup_button(void)
{
    // Configurer le bouton comme entrée avec pull-up
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << WAKEUP_BUTTON_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1,  // Pull-up interne activé
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        LOG_ESSENTIAL(TAG, "❌ Erreur configuration bouton: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // ESP32-C3 ne supporte pas EXT0 wakeup, utiliser GPIO wakeup à la place
    // Le bouton est sur GPIO9, réveil sur niveau bas (bouton appuyé)
    ret = esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKEUP_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (ret != ESP_OK) {
        LOG_ESSENTIAL(TAG, "❌ Erreur configuration réveil GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    LOG_DEBUG(TAG, "✅ Bouton de réveil configuré sur GPIO %d", WAKEUP_BUTTON_PIN);
    return ESP_OK;
}

// Fonction pour gérer le mode transfert BLE (désactivée temporairement)
// sert à gérer le mode transfert BLE (désactivée temporairement)
void handle_transfer_mode(void)
{
    LOG_ESSENTIAL(TAG, "🔘 Mode transfert BLE désactivé temporairement");
    LOG_ESSENTIAL(TAG, "💤 Retour au mode normal...");
    
    // Signal LED : BLE désactivé - Orange clignotant
    set_led_rgb(255, 165, 0, 2000, 5, 300, false);  // Orange 5 clignotements
}

#ifdef TEST_SHT45
// ============================================================================
// 🧪 MODE TEST SHT45 - Procédure temporaire de test sonde
// ============================================================================
void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));  // Attente connexion USB série
    
    ESP_LOGI(TAG, "🧪 === MODE TEST SHT45 ===");
    ESP_LOGI(TAG, "Bus I2C: SDA=GPIO%d, SCL=GPIO%d", I2C_SDA_PIN, I2C_SCL_PIN);
    
    // Créer le bus I2C directement (pas besoin du RTC)
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_PIN,
        .sda_io_num = I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    i2c_master_bus_handle_t bus_handle = NULL;
    esp_err_t ret = i2c_new_master_bus(&bus_config, &bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Impossible de créer le bus I2C (%s)", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "✅ Bus I2C initialisé");
    
    // Initialiser le SHT45
    ret = init_sht45(bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Impossible d'initialiser le SHT45 (%s)", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Vérifier le câblage: SDA→GPIO%d, SCL→GPIO%d, VCC→3.3V, GND→GND", I2C_SDA_PIN, I2C_SCL_PIN);
        i2c_del_master_bus(bus_handle);
        return;
    }
    
    ESP_LOGI(TAG, "✅ SHT45 détecté - Début des lectures (toutes les 2s)\n");
    ESP_LOGI(TAG, "  #  |  Température  |  Humidité");
    ESP_LOGI(TAG, "-----|--------------|----------");
    
    int count = 0;
    while (1) {
        sht45_data_t data;
        ret = read_sht45(&data);
        if (ret == ESP_OK) {
            count++;
            ESP_LOGI(TAG, "%3d  |   %6.2f °C   |  %5.2f %%", count, data.temperature, data.humidity);
        } else {
            ESP_LOGE(TAG, "  ❌  Erreur lecture SHT45 (%s)", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

#else
// ============================================================================
// 🦇 MODE NORMAL - Datalogger chiroptères
// ============================================================================
void app_main(void)
{
    // 🔌 DÉLAI USB SÉRIE - ESP32-C3 a besoin de temps pour établir la connexion USB
    // Commentez cette ligne en production pour économiser l'énergie
    vTaskDelay(pdMS_TO_TICKS(2000));  // 2 secondes pour connexion série
    
    ESP_LOGI(TAG, "🦇 Chiro Logger - Datalogger pour chiroptères");
    ESP_LOGI(TAG, "Version: 1.0.0");
    ESP_LOGI(TAG, "Plateforme: LOLIN C3 Mini (ESP32-C3)");
    
    // Diagnostic du réveil et gestion du compteur persistant
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    print_wakeup_info();
    
    // Configuration initiale
    LOG_DEBUG(TAG, "Initialisation du système...");
    
    #ifdef VISUAL_MODE
        // Initialiser la LED RGB pour feedback visuel (uniquement si VISUAL_MODE activé)
        esp_err_t ret = init_led_rgb();
        if (ret != ESP_OK) {
            LOG_ESSENTIAL(TAG, "⚠️  Impossible d'initialiser la LED RGB");
        } else {
            // Signal de démarrage : bleu pulsé
            set_led_rgb(0, 0, 255, 1000, 3, 300, false);  // Bleu 3 clignotements
        }
    #else
        LOG_DEBUG(TAG, "🔋 LED RGB désactivées (mode économie batterie)");
        esp_err_t ret;
    #endif
    
    // Initialiser le bouton de réveil pour le mode transfert
    ret = init_wakeup_button();
    if (ret != ESP_OK) {
        LOG_ESSENTIAL(TAG, "⚠️  Impossible d'initialiser le bouton de réveil");
    }
    
    // Initialiser et lire la tension batterie
    ret = init_battery();
    if (ret == ESP_OK) {
        battery_info_t bat;
        if (read_battery(&bat) == ESP_OK) {
            LOG_ESSENTIAL(TAG, "🔋 Batterie: %.2fV (%d%%)", bat.voltage, bat.percentage);
        } else {
            LOG_ESSENTIAL(TAG, "⚠️  Lecture batterie échouée");
        }
        deinit_battery();
    } else {
        LOG_ESSENTIAL(TAG, "⚠️  Impossible d'initialiser l'ADC batterie");
    }
    
    // Initialiser et lire l'horloge RTC DS1307
    char datetime_str[32] = {0};  // Horodatage réel pour les mesures
    bool sht45_ok = false;
    sht45_data_t sensor_data = { .temperature = -999.0f, .humidity = -999.0f };
    ret = init_rtc();
    if (ret == ESP_OK) {
        rtc_time_t now;
        if (rtc_get_time(&now) == ESP_OK) {
            // Si l'année est invalide, programmer depuis la date de compilation
            if (now.year < 2024) {
                LOG_ESSENTIAL(TAG, "⚠️  RTC non configurée, programmation auto...");
                rtc_set_time_from_compile();
                rtc_get_time(&now);  // Relire après programmation
            }
            rtc_format_datetime(&now, datetime_str, sizeof(datetime_str));
            LOG_ESSENTIAL(TAG, "🕐 RTC: %s", datetime_str);
        } else {
            LOG_ESSENTIAL(TAG, "⚠️  Lecture RTC échouée");
        }
        
        // Initialiser et lire le capteur SHT45 (partage le bus I2C avec le DS1307)
        i2c_master_bus_handle_t i2c_bus = rtc_get_i2c_bus();
        if (i2c_bus != NULL) {
            ret = init_sht45(i2c_bus);
            if (ret == ESP_OK) {
                if (read_sht45(&sensor_data) == ESP_OK) {
                    sht45_ok = true;
                    LOG_ESSENTIAL(TAG, "🌡️  SHT45: %.2f°C, %.2f%%", sensor_data.temperature, sensor_data.humidity);
                } else {
                    LOG_ESSENTIAL(TAG, "⚠️  Lecture SHT45 échouée");
                }
                deinit_sht45();
            } else {
                LOG_ESSENTIAL(TAG, "⚠️  Impossible d'initialiser le SHT45");
            }
        } else {
            LOG_ESSENTIAL(TAG, "⚠️  Bus I2C indisponible pour le SHT45");
        }
        
        deinit_rtc();
    } else {
        LOG_ESSENTIAL(TAG, "⚠️  Impossible d'initialiser la RTC DS1307");
    }
    
    // Fallback: si RTC indisponible, utiliser le temps depuis le boot
    if (datetime_str[0] == '\0') {
        int64_t timestamp = esp_timer_get_time() / 1000000;
        snprintf(datetime_str, sizeof(datetime_str), "%lld", (long long)timestamp);
        LOG_DEBUG(TAG, "⚠️  Horodatage de secours (uptime): %s", datetime_str);
    }
    
    // Vérifier si on a été réveillé par le bouton (mode transfert BLE)
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
        handle_transfer_mode();
        
        // Après le mode transfert, retourner en deep sleep immédiatement
        LOG_ESSENTIAL(TAG, "💤 Retour en deep sleep après mode transfert...");
        esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_SEC * 1000000ULL);
        esp_deep_sleep_start();
    }
    
    // Mode normal : effectuer une mesure
    
    // Initialiser le tampon flash énergétique
    ret = init_flash_buffer();
    if (ret != ESP_OK) {
        LOG_ESSENTIAL(TAG, "❌ Impossible d'initialiser le tampon flash");
        LOG_ESSENTIAL(TAG, "Mode dégradé: écriture directe sur SD");
        // Continuer en mode dégradé si le tampon flash échoue
    } else {
        LOG_DEBUG(TAG, "✅ Tampon flash initialisé");
    }
    
    // Si c'est un démarrage frais (perte d'alimentation), restaurer le compteur
    // depuis les données existantes (tampon SPIFFS + carte SD)
    if (cycle_counter == 0) {
        restore_cycle_counter_from_storage();
    }
    
    // Boucle principale - effectuer UNE mesure puis dormir
    cycle_counter++; // Incrémenter le compteur à chaque réveil (persiste grâce à RTC_DATA_ATTR)
    
    LOG_ESSENTIAL(TAG, "📊 Cycle de mesure #%d", cycle_counter);
    
    // Signal LED de début de cycle (déjà fait au démarrage en bleu)
    
    // Effectuer une mesure (réelle si SHT45 disponible, simulée sinon)
    float temp, humidity;
    if (sht45_ok) {
        temp = sensor_data.temperature;
        humidity = sensor_data.humidity;
    } else {
        // Fallback : valeurs simulées si capteur indisponible
        temp = 18.5 + (cycle_counter * 0.1);
        humidity = 85.0 + (cycle_counter * 0.2);
        LOG_ESSENTIAL(TAG, "⚠️  Mesure simulée (SHT45 indisponible)");
    }
    
    LOG_DEBUG(TAG, "🌡️  Mesure: T=%.1f°C, H=%.1f%%", temp, humidity);
    
    // L'horodatage RTC (datetime_str) a été lu plus haut lors de l'init RTC
    
    // Ajouter la mesure au tampon flash (mode économie d'énergie) avec ID unique
    esp_err_t buffer_result = add_to_flash_buffer(cycle_counter, datetime_str, temp, humidity);
    if (buffer_result == ESP_OK) {
        LOG_DEBUG(TAG, "🔋 Mesure stockée dans le tampon flash");
        
        // Vérifier si il faut faire un flush vers la SD
        int buffer_count = count_buffer_lines();
        LOG_ESSENTIAL(TAG, "📊 Tampon: %d/%d mesures", buffer_count, BUFFER_FLUSH_THRESHOLD);
        
        if (buffer_count >= BUFFER_FLUSH_THRESHOLD) {
            LOG_ESSENTIAL(TAG, "🔄 Seuil atteint - flush vers la carte SD...");
            // Signal LED : flush en cours - Cyan pulsé
            set_led_rgb(0, 255, 255, 500, 2, 200, false);  // Cyan 2 clignotements rapides
            
            esp_err_t flush_result = flush_buffer_to_sd();
            if (flush_result == ESP_OK) {
                LOG_ESSENTIAL(TAG, "✅ Flush réussi - tampon vidé");
                // Signal LED : succès - Vert fixe
                set_led_rgb(0, 255, 0, 500, 0, 0, false);  // Vert fixe 500ms
            } else {
                LOG_ESSENTIAL(TAG, "⚠️  Flush échoué - données conservées dans le tampon");
                // Signal LED : avertissement - Jaune clignotant
                set_led_rgb(255, 255, 0, 1000, 3, 250, false);  // Jaune 3 clignotements
            }
        } else {
            // Mesure normale stockée - Vert flash rapide
            set_led_rgb(0, 255, 0, 100, 1, 0, false);  // Vert 1 flash rapide
        }
    } else {
        LOG_ESSENTIAL(TAG, "⚠️  Échec stockage tampon - tentative écriture directe SD");
        
        // Mode dégradé: écriture directe sur SD
        esp_err_t sd_result = init_sd_card();
        if (sd_result == ESP_OK) {
            // Créer le répertoire CHIRO s'il n'existe pas
            struct stat st2;
            if (stat("/sdcard/CHIRO", &st2) != 0) {
                mkdir("/sdcard/CHIRO", 0775);
            }
            esp_err_t csv_result = log_data_to_csv("/sdcard/CHIRO/data.csv", 
                                                   cycle_counter, datetime_str, temp, humidity);
            if (csv_result == ESP_OK) {
                LOG_ESSENTIAL(TAG, "💾 Données sauvegardées directement sur SD");
                // Signal LED : mode dégradé OK - Vert lent
                set_led_rgb(0, 255, 0, 800, 2, 400, false);  // Vert 2 clignotements lents
            } else {
                LOG_ESSENTIAL(TAG, "❌ Échec sauvegarde directe sur SD");
                // Signal LED : erreur - Rouge clignotant rapide
                set_led_rgb(255, 0, 0, 1000, 5, 150, false);  // Rouge 5 clignotements rapides
            }
            unmount_sd_card();
        } else {
            LOG_ESSENTIAL(TAG, "❌ Données perdues - tampon et SD indisponibles");
            // Signal LED : erreur critique - Rouge fixe long
            set_led_rgb(255, 0, 0, 2000, 0, 0, false);  // Rouge fixe 2s
        }
    }
    
    // Configurer le deep sleep timer ET le réveil par bouton
    LOG_DEBUG(TAG, "💤 Entrée en deep sleep pour %d secondes...", DEEP_SLEEP_DURATION_SEC);
    LOG_DEBUG(TAG, "🔘 Réveil possible par bouton GPIO %d pour mode transfert", WAKEUP_BUTTON_PIN);
    
    // Signal LED avant deep sleep - Bleu fade out
    set_led_rgb(0, 0, 50, 500, 1, 0, false);  // Bleu dim 1 flash
    led_off();  // Éteindre explicitement
    
    // Configurer le réveil par timer
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_SEC * 1000000ULL); // Convertir en microsecondes
    
    // Le réveil par bouton est déjà configuré par init_wakeup_button()
    
    // Entrer en deep sleep
    esp_deep_sleep_start();
    
    // Cette ligne ne sera jamais exécutée car l'ESP32 redémarre après le deep sleep
}
#endif // TEST_SHT45