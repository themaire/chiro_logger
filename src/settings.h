#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include "config.h"

// Structure globale de configuration
typedef struct {
    uint32_t deep_sleep_duration_sec;  // Durée du sommeil profond (secondes)
    uint32_t buffer_flush_threshold;   // Seuil de flush tampon (nombre de mesures)
    int      wakeup_button_pin;        // GPIO du bouton de réveil
    float    sht45_temp_offset;        // Calibration Température (°C)
    float    sht45_humidity_offset;    // Calibration Humidité (%RH)
    bool     visual_mode;              // Activer/Désactiver LED RGB (true=1, false=0)
    bool     production_mode;          // Activer/Désactiver logs production (true=1, false=0)
} system_settings_t;

// Variable globale accessible partout (en lecture seule recommandée)
extern system_settings_t g_settings;

/**
 * @brief Initialise les réglages avec les valeurs par défaut
 */
void init_settings_defaults(void);

/**
 * @brief Charge la configuration depuis la carte SD
 * 
 * Si le fichier existe, il surcharge les valeurs par défaut.
 * Si le fichier n'existe pas, il est créé avec les valeurs actuelles.
 * 
 * @return ESP_OK si succès (chargé ou créé), ESP_FAIL si erreur critique
 */
esp_err_t load_settings_from_sd(void);

#endif // SETTINGS_H
