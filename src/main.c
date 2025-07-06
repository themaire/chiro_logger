#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <sys/stat.h>
#include <time.h>

static const char *TAG = "CHIRO_LOGGER";

// Configuration des pins pour le slot SD sur LOLIN D32 PRO
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   4

// Point de montage de la carte SD
#define MOUNT_POINT "/sdcard"

// Fonction utilitaire pour enregistrer des données au format CSV
esp_err_t log_data_to_csv(const char* filepath, const char* datetime, float temperature, float humidity)
{
    // Valeurs par défaut si les paramètres sont NULL ou invalides
    if (filepath == NULL) {
        filepath = "/sdcard/chiro/data.csv";
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
        fprintf(file, "DateTime,Temperature_C,Humidity_%%\n");
    }
    
    // Écrire les données
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
        ESP_LOGE(TAG, "Erreur initialisation bus SPI: %s", esp_err_to_name(ret));
        return ret;
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
    const char* work_dir = "/sdcard/chiro";
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
    const char* test_file = "/sdcard/chiro/test.txt";
    ESP_LOGI(TAG, "Tentative d'écriture dans: %s", test_file);
    
    FILE *f = fopen(test_file, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Erreur: impossible d'ouvrir %s pour écriture", test_file);
        
        // Essayer avec un nom de fichier différent dans le répertoire racine
        test_file = "/sdcard/chiro.txt";
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

void app_main(void)
{
    ESP_LOGI(TAG, "🦇 Chiro Logger - Datalogger pour chiroptères");
    ESP_LOGI(TAG, "Version: 1.0.0");
    ESP_LOGI(TAG, "Plateforme: LOLIN D32 PRO (ESP32)");
    
    // Configuration initiale
    ESP_LOGI(TAG, "Initialisation du système...");
    
    // Initialisation de la carte SD
    esp_err_t ret = init_sd_card();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur lors de l'initialisation de la carte SD");
        ESP_LOGI(TAG, "Le système continue sans carte SD...");
    } else {
        ESP_LOGI(TAG, "✅ Carte SD initialisée avec succès");
        
        // Test d'écriture
        ret = test_sd_card();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✅ Test d'écriture SD réussi");
        } else {
            ESP_LOGE(TAG, "❌ Test d'écriture SD échoué");
        }
    }
    
    // Boucle principale
    int counter = 0;
    while (1) {
        ESP_LOGI(TAG, "Système en fonctionnement - Cycle %d", counter++);
        
        // Simulation d'une mesure toutes les 10 secondes (tous les 2 cycles de 5s)
        if (counter % 2 == 0) {
            float temp = 18.5 + (counter * 0.1);
            float humidity = 85.0 + (counter * 0.2);
            
            ESP_LOGI(TAG, "📊 Simulation: T=%.1f°C, H=%.1f%%", temp, humidity);
            
            // Enregistrer les données dans le fichier CSV (seulement si la SD est disponible)
            if (ret == ESP_OK) {
                // Générer un timestamp simple pour la démonstration
                char datetime_str[32];
                int64_t timestamp = esp_timer_get_time() / 1000000; // Secondes depuis le démarrage
                snprintf(datetime_str, sizeof(datetime_str), "%lld", (long long)timestamp);
                
                esp_err_t csv_result = log_data_to_csv("/sdcard/chiro/measurements.csv", 
                                                       datetime_str, temp, humidity);
                if (csv_result == ESP_OK) {
                    ESP_LOGI(TAG, "💾 Données sauvegardées sur SD");
                } else {
                    ESP_LOGW(TAG, "⚠️  Échec sauvegarde sur SD");
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // Attendre 5 secondes
    }
}
