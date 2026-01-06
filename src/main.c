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
// #include "ble_manager.h"

static const char *TAG = "CHIRO_LOGGER";

// Variable stockée en RTC memory pour persister entre les deep sleeps
RTC_DATA_ATTR int cycle_counter = 0;

// Configuration des pins pour le slot SD sur LOLIN D32 PRO
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   4

// Configuration LED pour feedback visuel
#define LED_PIN 5  // LED intégrée sur LOLIN D32 PRO (alternative si GPIO 2 ne fonctionne pas)

// Configuration du bouton de réveil pour mode transfert BLE
#define WAKEUP_BUTTON_PIN GPIO_NUM_0  // Bouton BOOT sur LOLIN D32 PRO
#define WAKEUP_BUTTON_LEVEL 0         // Niveau bas quand appuyé (pull-up interne)

// Point de montage de la carte SD
#define MOUNT_POINT "/sdcard"

// Point de montage du tampon flash (partition SPIFFS)
#define BUFFER_MOUNT_POINT "/buffer"

// Configuration du deep sleep (en secondes)
#define DEEP_SLEEP_DURATION_SEC 5

// Configuration des logs pour économie d'énergie (décommenter pour production)
#define PRODUCTION_MODE  // Désactive la plupart des logs pour économiser l'énergie

/*
 * 💡 OPTIMISATION ÉNERGÉTIQUE - LOGS :
 * 
 * Les ESP_LOGI/ESP_LOGE consomment de l'énergie car ils :
 * - Maintiennent l'UART actif (~10-20 mA)
 * - Prolongent le temps d'activité avant deep sleep
 * - Formatent et transmettent les chaînes
 * 
 * EN PRODUCTION : Décommenter #define PRODUCTION_MODE
 * - Garde uniquement les logs essentiels (erreurs, flush, compteur)
 * - Supprime les logs de debug/verbose
 * - Économie estimée : 5-10% d'autonomie supplémentaire
 */

// Configuration du tampon flash pour économie d'énergie
#define BUFFER_FLUSH_THRESHOLD 500  // Nombre de mesures avant flush vers SD (optimisé pour autonomie)

#define BUFFER_CSV_FILE "/buffer/data_buffer.csv"

// Déclarations de fonctions
esp_err_t init_led(void);
void blink_led(int count, int delay_ms);
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

// Fonction utilitaire pour enregistrer des données au format CSV avec ID unique
esp_err_t log_data_to_csv(const char* filepath, int id, const char* datetime, float temperature, float humidity)
{
    // Valeurs par défaut si les paramètres sont NULL ou invalides
    if (filepath == NULL) {
        filepath = "/sdcard/CHIRO/data.csv";
    }
    
    // Vérifier si le fichier existe déjà
    FILE *file = fopen(filepath, "r");
    bool file_exists = (file != NULL);
    if (file != NULL) {
        fclose(file);
    }
    
    // Ouvrir le fichier en mode append
    file = fopen(filepath, "a");
    if (file == NULL) {
        ESP_LOGE(TAG, "❌ Impossible d'ouvrir le fichier CSV: %s", filepath);
        return ESP_FAIL;
    }
    
    // Si le fichier n'existait pas, écrire l'en-tête
    if (!file_exists) {
        ESP_LOGI(TAG, "📄 Création du fichier CSV avec en-tête: %s", filepath);
        fprintf(file, "ID,DateTime,Temperature_C,Humidity_%%\n");
    }
    
    // Écrire les données avec ID unique en première colonne
    fprintf(file, "%d,", id);
    
    if (datetime == NULL) {
        // Générer un timestamp par défaut basé sur esp_timer
        int64_t timestamp = esp_timer_get_time() / 1000000; // Convertir en secondes
        fprintf(file, "%lld,", (long long)timestamp);
    } else {
        fprintf(file, "%s,", datetime);
    }
    
    // Gérer les valeurs par défaut pour température et humidité
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
    ESP_LOGI(TAG, "✅ Données enregistrées dans: %s", filepath);
    return ESP_OK;
}

// Fonction d'initialisation de la carte SD
esp_err_t init_sd_card(void)
{
    ESP_LOGI(TAG, "Initialisation de la carte microSD...");
    
    esp_err_t ret;
    
    // Configuration du host SPI pour la carte SD
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,  // ⚠️ SÉCURITÉ: Pas de formatage automatique pour préserver les données
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false  // Désactiver la vérification de statut
    };
    
    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    
    ESP_LOGI(TAG, "Initialisation du bus SPI...");
    
    // Configuration du bus SPI
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_STATE) {
            // Le bus SPI est déjà initialisé, c'est normal lors d'une récupération
            ESP_LOGI(TAG, "Bus SPI déjà initialisé (récupération)");
        } else {
            ESP_LOGE(TAG, "Erreur initialisation bus SPI: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    // Configuration du slot SPI pour la carte SD
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;
    
    ESP_LOGI(TAG, "Montage du système de fichiers FAT...");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "❌ Impossible de monter le système de fichiers FAT.");
            ESP_LOGW(TAG, "💡 Solutions possibles:");
            ESP_LOGW(TAG, "   1. Vérifiez que la carte SD est bien insérée");
            ESP_LOGW(TAG, "   2. Formatez la carte SD en FAT32 sur votre ordinateur");
            ESP_LOGW(TAG, "   3. Utilisez une carte SD différente");
            ESP_LOGW(TAG, "   4. Vérifiez que la carte SD n'est pas corrompue");
            ESP_LOGW(TAG, "ℹ️  Le formatage automatique est désactivé pour préserver vos données");
        } else {
            ESP_LOGE(TAG, "❌ Erreur initialisation carte (%s).", esp_err_to_name(ret));
            ESP_LOGW(TAG, "💡 Vérifiez que la carte SD est insérée et correctement connectée.");
        }
        return ret;
    }
    ESP_LOGI(TAG, "Système de fichiers monté");
    
    // Affichage des informations de la carte
    sdmmc_card_print_info(stdout, card);
    
    // Vérifier les permissions du répertoire racine
    ESP_LOGI(TAG, "Vérification des permissions du répertoire %s", mount_point);
    
    // Créer le répertoire de travail pour le projet
    const char* work_dir = "/sdcard/CHIRO";
    struct stat st;
    if (stat(work_dir, &st) != 0) {
        ESP_LOGI(TAG, "Création du répertoire de travail: %s", work_dir);
        if (mkdir(work_dir, 0755) != 0) {
            ESP_LOGW(TAG, "Impossible de créer le répertoire %s", work_dir);
        } else {
            ESP_LOGI(TAG, "✅ Répertoire de travail créé: %s", work_dir);
        }
    } else {
        ESP_LOGI(TAG, "✅ Répertoire de travail existe déjà: %s", work_dir);
    }
    
    return ESP_OK;
}

// Fonction de test d'écriture sur la carte SD
esp_err_t test_sd_card(void)
{
    ESP_LOGI(TAG, "Test d'écriture sur la carte SD...");
    
    // Vérifier que le point de montage existe
    struct stat st;
    if (stat(MOUNT_POINT, &st) != 0) {
        ESP_LOGE(TAG, "Point de montage %s n'existe pas", MOUNT_POINT);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Point de montage %s existe", MOUNT_POINT);
    
    // Essayer d'abord avec un chemin dans le répertoire du projet
    const char* test_file = "/sdcard/CHIRO/test.txt";
    ESP_LOGI(TAG, "Tentative d'écriture dans: %s", test_file);
    
    FILE *f = fopen(test_file, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erreur: impossible d'ouvrir %s pour écriture", test_file);
        
        // Essayer avec un nom de fichier différent dans le répertoire racine
        test_file = "/sdcard/CHIRO.txt";
        ESP_LOGI(TAG, "Tentative avec un autre nom: %s", test_file);
        f = fopen(test_file, "w");
        
        if (f == NULL) {
            ESP_LOGE(TAG, "Erreur: impossible d'ouvrir %s pour écriture", test_file);
            ESP_LOGE(TAG, "💡 Vérifiez les permissions de la carte SD");
            return ESP_FAIL;
        }
    }
    
    // Écrire des données simples
    fprintf(f, "Chiro Logger Test\n");
    fprintf(f, "Timestamp: %lld\n", (long long)esp_timer_get_time());
    fprintf(f, "Status: OK\n");
    fclose(f);
    
    ESP_LOGI(TAG, "Fichier %s écrit avec succès", test_file);
    
    // Lire le fichier pour vérifier
    f = fopen(test_file, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erreur: impossible d'ouvrir le fichier pour lecture");
        return ESP_FAIL;
    }
    
    char line[64];
    ESP_LOGI(TAG, "Contenu du fichier de test:");
    while (fgets(line, sizeof(line), f)) {
        // Retirer le \n pour l'affichage
        char* pos = strchr(line, '\n');
        if (pos) *pos = '\0';
        ESP_LOGI(TAG, "  %s", line);
    }
    fclose(f);
    
    return ESP_OK;
}

// Fonction pour démonter proprement la carte SD
esp_err_t unmount_sd_card(void)
{
    ESP_LOGI(TAG, "Démontage de la carte SD...");
    
    // Démonter le système de fichiers
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Erreur lors du démontage: %s", esp_err_to_name(ret));
        // Continuer même en cas d'erreur
    }
    
    // NE PAS libérer le bus SPI automatiquement - cela cause des crashes
    // Le bus sera automatiquement réinitialisé lors de la prochaine tentative de montage
    ESP_LOGI(TAG, "Démontage terminé (bus SPI conservé)");
    return ESP_OK;
}

// Fonction d'initialisation du tampon flash (partition SPIFFS)
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
    
    // Clignotement LED : 1 fois pour ajout au tampon
    blink_led(1, 100);
    
    return ESP_OK;
}

// Fonction pour compter les lignes dans le tampon flash
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

// Fonction pour transférer le tampon flash vers la carte SD
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
    
    // Clignotement LED : 20 fois pour flush vers SD
    blink_led(10, 30);
    
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

// Fonction d'initialisation de la LED
esp_err_t init_led(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_PIN),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        gpio_set_level(LED_PIN, 0); // LED éteinte au démarrage
        // ESP_LOGI(TAG, "✅ LED initialisée sur pin %d", LED_PIN);
    }
    return ret;
}

// Fonction pour faire clignoter la LED
void blink_led(int count, int delay_ms)
{
    // ESP_LOGI(TAG, "💡 LED: %d clignotement(s)", count);
    for (int i = 0; i < count; i++) {
        gpio_set_level(LED_PIN, 1); // Allumer
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        gpio_set_level(LED_PIN, 0); // Éteindre
        if (i < count - 1) { // Pas de délai après le dernier clignotement
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }
}

// Fonction de diagnostic du réveil
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
void handle_transfer_mode(void)
{
    LOG_ESSENTIAL(TAG, "🔘 Mode transfert BLE désactivé temporairement");
    LOG_ESSENTIAL(TAG, "💤 Retour au mode normal...");
    
    // Signal LED : BLE désactivé
    blink_led(3, 100);
}

void app_main(void)
{
    ESP_LOGI(TAG, "🦇 Chiro Logger - Datalogger pour chiroptères");
    ESP_LOGI(TAG, "Version: 1.0.0");
    ESP_LOGI(TAG, "Plateforme: LOLIN D32 PRO (ESP32)");
    
    // Diagnostic du réveil et gestion du compteur persistant
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    print_wakeup_info();
    
    // Configuration initiale
    LOG_DEBUG(TAG, "Initialisation du système...");
    
    // Initialiser la LED pour feedback visuel
    esp_err_t ret = init_led();
    if (ret != ESP_OK) {
        LOG_ESSENTIAL(TAG, "⚠️  Impossible d'initialiser la LED");
    }
    
    // Initialiser le bouton de réveil pour le mode transfert
    ret = init_wakeup_button();
    if (ret != ESP_OK) {
        LOG_ESSENTIAL(TAG, "⚠️  Impossible d'initialiser le bouton de réveil");
    }
    
    // Vérifier si on a été réveillé par le bouton (mode transfert BLE)
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
        handle_transfer_mode();
        
        // Après le mode transfert, retourner en deep sleep immédiatement
        LOG_ESSENTIAL(TAG, "� Retour en deep sleep après mode transfert...");
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
    
    // Boucle principale - effectuer UNE mesure puis dormir
    cycle_counter++; // Incrémenter le compteur à chaque réveil (persiste grâce à RTC_DATA_ATTR)
    
    LOG_ESSENTIAL(TAG, "📊 Cycle de mesure #%d", cycle_counter);
    
    // Signal LED de début de cycle
    blink_led(1, 30);
    
    // Effectuer une mesure
    float temp = 18.5 + (cycle_counter * 0.1);
    float humidity = 85.0 + (cycle_counter * 0.2);
    
    LOG_DEBUG(TAG, "🌡️  Mesure: T=%.1f°C, H=%.1f%%", temp, humidity);
    
    // Générer un timestamp
    char datetime_str[32];
    int64_t timestamp = esp_timer_get_time() / 1000000;
    snprintf(datetime_str, sizeof(datetime_str), "%lld", (long long)timestamp);
    
    // Ajouter la mesure au tampon flash (mode économie d'énergie) avec ID unique
    esp_err_t buffer_result = add_to_flash_buffer(cycle_counter, datetime_str, temp, humidity);
    if (buffer_result == ESP_OK) {
        LOG_DEBUG(TAG, "🔋 Mesure stockée dans le tampon flash");
        
        // Vérifier si il faut faire un flush vers la SD
        int buffer_count = count_buffer_lines();
        LOG_ESSENTIAL(TAG, "📊 Tampon: %d/%d mesures", buffer_count, BUFFER_FLUSH_THRESHOLD);
        
        if (buffer_count >= BUFFER_FLUSH_THRESHOLD) {
            LOG_ESSENTIAL(TAG, "🔄 Seuil atteint - flush vers la carte SD...");
            esp_err_t flush_result = flush_buffer_to_sd();
            if (flush_result == ESP_OK) {
                LOG_ESSENTIAL(TAG, "✅ Flush réussi - tampon vidé");
            } else {
                LOG_ESSENTIAL(TAG, "⚠️  Flush échoué - données conservées dans le tampon");
            }
        }
    } else {
        LOG_ESSENTIAL(TAG, "⚠️  Échec stockage tampon - tentative écriture directe SD");
        
        // Mode dégradé: écriture directe sur SD
        esp_err_t sd_result = init_sd_card();
        if (sd_result == ESP_OK) {
            esp_err_t csv_result = log_data_to_csv("/sdcard/CHIRO/data.csv", 
                                                   cycle_counter, datetime_str, temp, humidity);
            if (csv_result == ESP_OK) {
                LOG_ESSENTIAL(TAG, "💾 Données sauvegardées directement sur SD");
            } else {
                LOG_ESSENTIAL(TAG, "❌ Échec sauvegarde directe sur SD");
            }
            unmount_sd_card();
        } else {
            LOG_ESSENTIAL(TAG, "❌ Données perdues - tampon et SD indisponibles");
        }
    }
    
    // Configurer le deep sleep timer ET le réveil par bouton
    LOG_DEBUG(TAG, "💤 Entrée en deep sleep pour %d secondes...", DEEP_SLEEP_DURATION_SEC);
    LOG_DEBUG(TAG, "🔘 Réveil possible par bouton GPIO %d pour mode transfert", WAKEUP_BUTTON_PIN);
    
    // Configurer le réveil par timer
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_SEC * 1000000ULL); // Convertir en microsecondes
    
    // Le réveil par bouton est déjà configuré par init_wakeup_button()
    
    // Entrer en deep sleep
    esp_deep_sleep_start();
    
    // Cette ligne ne sera jamais exécutée car l'ESP32 redémarre après le deep sleep
}