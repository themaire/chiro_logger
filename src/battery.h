#ifndef BATTERY_H
#define BATTERY_H

#include <esp_err.h>

/**
 * @file battery.h
 * @brief Mesure de la tension batterie via ADC
 * 
 * Ce module utilise l'ADC1 de l'ESP32-C3 sur GPIO3 pour mesurer
 * la tension de la batterie LiPo via le diviseur de tension intégré
 * au LOLIN C3 Mini.
 * 
 * La tension est convertie en pourcentage selon la courbe de décharge
 * typique d'une cellule LiPo (4.2V = 100%, 3.0V = 0%).
 */

/**
 * @brief Résultat de la mesure batterie
 */
typedef struct {
    float voltage;      /**< Tension batterie en Volts (ex: 3.85) */
    int percentage;     /**< Niveau de charge en % (0-100) */
} battery_info_t;

/**
 * @brief Initialise l'ADC pour la lecture de la tension batterie
 * 
 * Configure ADC1_CHANNEL_3 (GPIO3) avec l'atténuation 11dB
 * pour lire la plage 0-3.1V environ (suffisant pour diviseur de tension).
 * 
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t init_battery(void);

/**
 * @brief Lit la tension batterie et calcule le pourcentage
 * 
 * Effectue plusieurs lectures ADC et les moyenne pour un résultat stable.
 * La tension brute ADC est convertie en tension réelle via le facteur
 * du diviseur de tension (×2 sur LOLIN C3 Mini).
 * 
 * @param[out] info Structure contenant la tension (V) et le pourcentage (%)
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t read_battery(battery_info_t *info);

/**
 * @brief Libère les ressources ADC
 * 
 * @return ESP_OK en cas de succès
 */
esp_err_t deinit_battery(void);

#endif // BATTERY_H
