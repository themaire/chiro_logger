#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include "settings.h"
#include "sd_card.h"
#include "config.h" // Pour les valeurs par défaut

static const char *TAG = "SETTINGS";
static const char *CONFIG_FILENAME = "/sdcard/config.txt";

system_settings_t g_settings;

// Initialisation avec les macros de config.h
void init_settings_defaults(void) {
    g_settings.deep_sleep_duration_sec = DEEP_SLEEP_DURATION_SEC_DEFAULT;
    g_settings.buffer_flush_threshold = BUFFER_FLUSH_THRESHOLD_DEFAULT;
    g_settings.wakeup_button_pin = WAKEUP_BUTTON_PIN;
    g_settings.sht45_temp_offset = SHT45_TEMP_OFFSET_DEFAULT;
    g_settings.sht45_humidity_offset = SHT45_HUMIDITY_OFFSET_DEFAULT;
    
    #ifdef VISUAL_MODE_DEFAULT
    g_settings.visual_mode = true;
    #else
    g_settings.visual_mode = false;
    #endif

    #ifdef PRODUCTION_MODE_DEFAULT
    g_settings.production_mode = true;
    #else
    g_settings.production_mode = false;
    #endif
}

// Helper pour supprimer les espaces avant/après
static char* trim(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    *(end+1) = 0;
    return str;
}

// Helper pour parser une ligne "KEY=VALUE"
static void parse_line(char *line) {
    char *key = strtok(line, "=");
    char *value = strtok(NULL, "\n"); // Prend jusqu'à la fin de ligne
    
    if (key && value) {
        key = trim(key);
        value = trim(value);
        
        // Supprimer d'éventuels commentaires in-line (après un # ou //)
        char *comment = strchr(value, '#');
        if (comment) *comment = '\0';
        // Note: strchr avec "//" est plus complexe en C standard rapide, on s'en tient au # standard conf
        
        value = trim(value); // Re-trim après suppression commentaire

        if (strcmp(key, "DEEP_SLEEP_SEC") == 0) {
            g_settings.deep_sleep_duration_sec = atoi(value);
            ESP_LOGI(TAG, "  -> Sleep Duration: %lu sec", (unsigned long)g_settings.deep_sleep_duration_sec);
        }
        else if (strcmp(key, "FLUSH_THRESHOLD") == 0) {
            g_settings.buffer_flush_threshold = atoi(value);
            ESP_LOGI(TAG, "  -> Flush Threshold: %lu", (unsigned long)g_settings.buffer_flush_threshold);
        }
        else if (strcmp(key, "TEMP_OFFSET") == 0) {
            g_settings.sht45_temp_offset = atof(value);
            ESP_LOGI(TAG, "  -> Temp Offset: %.2f", g_settings.sht45_temp_offset);
        }
        else if (strcmp(key, "HUM_OFFSET") == 0) {
            g_settings.sht45_humidity_offset = atof(value);
            ESP_LOGI(TAG, "  -> Hum Offset: %.2f", g_settings.sht45_humidity_offset);
        }
        else if (strcmp(key, "VISUAL_MODE") == 0) {
            g_settings.visual_mode = (atoi(value) == 1 || strcasecmp(value, "true") == 0 || strcasecmp(value, "YES") == 0);
            ESP_LOGI(TAG, "  -> Visual Mode: %s", g_settings.visual_mode ? "ON" : "OFF");
        }
         else if (strcmp(key, "PRODUCTION_MODE") == 0) {
            g_settings.production_mode = (atoi(value) == 1 || strcasecmp(value, "true") == 0 || strcasecmp(value, "YES") == 0);
             ESP_LOGI(TAG, "  -> Production Mode: %s", g_settings.production_mode ? "ON" : "OFF");
        }
    }
}

// Créer le fichier par défaut
static void create_default_config_file(void) {
    ESP_LOGI(TAG, "Création du fichier de configuration par défaut...");
    FILE *f = fopen(CONFIG_FILENAME, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Impossible de créer %s", CONFIG_FILENAME);
        return;
    }

    fprintf(f, "# Configuration Chiro Logger\n");
    fprintf(f, "# Editez ce fichier avec le Bloc-notes. Ne changez pas les noms des variables ci-dessous(EEP_SLEEP_SEC=...). \n");
    fprintf(f, "# Redémarrez le logger pour appliquer les changements.\n\n");

    fprintf(f, "# Duree entre deux mesures (secondes). Ex: 300=5min, 1800=30min\n");
    fprintf(f, "DEEP_SLEEP_SEC=%lu\n\n", (unsigned long)g_settings.deep_sleep_duration_sec);

    fprintf(f, "# Nombre de mesures avant ecriture sur carte SD (pour economie energie)\n");
    fprintf(f, "FLUSH_THRESHOLD=%lu\n\n", (unsigned long)g_settings.buffer_flush_threshold);

    fprintf(f, "# Calibration sonde temp (Offset en degres C). Ex: -0.5\n");
    fprintf(f, "TEMP_OFFSET=%.2f\n\n", g_settings.sht45_temp_offset);

    fprintf(f, "# Calibration sonde humidite (Offset en %%). Ex: 0.0\n");
    fprintf(f, "HUM_OFFSET=%.2f\n\n", g_settings.sht45_humidity_offset);

    fprintf(f, "# Activer les LED RGB (1=OUI, 0=NON). Mettre 0 pour economiser la batterie.\n");
    fprintf(f, "VISUAL_MODE=%d\n\n", g_settings.visual_mode ? 1 : 0);

    fprintf(f, "# Mode Production (1=OUI, 0=NON). 1=Moins de logs, plus d'economie.\n");
    fprintf(f, "PRODUCTION_MODE=%d\n", g_settings.production_mode ? 1 : 0);

    fclose(f);
    ESP_LOGI(TAG, "Fichier %s créé avec succès.", CONFIG_FILENAME);
}

esp_err_t load_settings_from_sd(void) {
    // 1. Charger les defaults d'abord
    init_settings_defaults();

    // 2. Tenter d'initialiser la SD (si pas déjà fait)
    // Note: On suppose que init_sd_card() gère le cas où c'est déjà monté, ou on démonte à la fin
    esp_err_t ret = init_sd_card();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Carte SD non détectée, utilisation des valeurs par défaut.");
        return ret; // Pas grave, on continue avec les défauts
    }

    // 3. Vérifier présence fichier
    struct stat st;
    if (stat(CONFIG_FILENAME, &st) != 0) {
        // Fichier n'existe pas -> Création
        ESP_LOGW(TAG, "%s introuvable, création du modèle...", CONFIG_FILENAME);
        create_default_config_file();
    } else {
        // Fichier existe -> Lecture
        ESP_LOGI(TAG, "Lecture de la configuration depuis SD...");
        FILE *f = fopen(CONFIG_FILENAME, "r");
        if (f != NULL) {
            char line[128];
            while (fgets(line, sizeof(line), f)) {
                // Ignorer lignes vides ou commentaires purs
                if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
                parse_line(line);
            }
            fclose(f);
        } else {
            ESP_LOGE(TAG, "Erreur lecture fichier config.");
        }
    }

    // 4. Démonter SD pour économie énergie (sauf si besoin immédiat après)
    unmount_sd_card();
    
    return ESP_OK;
}
