#ifndef LED_RGB_H
#define LED_RGB_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

/**
 * @file led_rgb.h
 * @brief Contrôle de la LED RGB WS2812 pour feedback visuel
 * 
 * Ce module fournit des fonctions pour contrôler la LED RGB WS2812
 * via le périphérique RMT de l'ESP32-C3.
 * 
 * Compilation conditionnelle : Toutes les fonctions sont désactivées
 * si VISUAL_MODE n'est pas défini (économie batterie).
 */

/**
 * @brief Initialise la LED RGB WS2812
 * 
 * Configure le driver RMT pour contrôler la LED WS2812 sur GPIO7.
 * Ne fait rien si VISUAL_MODE n'est pas défini.
 * 
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t init_led_rgb(void);

/**
 * @brief Contrôle la LED RGB avec options de clignotement
 * 
 * @param r Composante rouge (0-255)
 * @param g Composante verte (0-255)
 * @param b Composante bleue (0-255)
 * @param duration_ms Durée totale d'allumage en millisecondes
 * @param blink_count Nombre de clignotements (0 = allumage continu)
 * @param blink_period_ms Période entre clignotements en millisecondes (ignoré si blink_count = 0)
 * @param force_display Force l'affichage même si VISUAL_MODE désactivé (pour erreurs critiques)
 * 
 * Note: Ne fait rien si VISUAL_MODE n'est pas défini, SAUF si force_display = true
 * 
 * Exemples d'utilisation:
 * - set_led_rgb(255, 0, 0, 1000, 0, 0, false);        // Rouge fixe pendant 1s
 * - set_led_rgb(0, 255, 0, 2000, 5, 200, false);      // Vert clignotant 5x avec 200ms entre clignotements
 * - set_led_rgb(255, 0, 0, 3000, 10, 150, true);      // Rouge clignotant FORCÉ même en production
 */
void set_led_rgb(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms, uint8_t blink_count, uint32_t blink_period_ms, bool force_display);

/**
 * @brief Éteint la LED RGB
 * 
 * Note: Ne fait rien si VISUAL_MODE n'est pas défini
 */
void led_off(void);

#endif // LED_RGB_H
