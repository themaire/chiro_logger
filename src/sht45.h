#ifndef SHT45_H
#define SHT45_H

#include <esp_err.h>
#include <driver/i2c_master.h>

/**
 * @file sht45.h
 * @brief Driver capteur SHT45 (température + humidité) via I2C
 * 
 * Ce module pilote le capteur Sensirion SHT45 sur le bus I2C partagé
 * avec le DS1307 (SDA=GPIO8, SCL=GPIO10 sur LOLIN C3 Mini).
 * 
 * Le SHT45 est un capteur haute précision :
 * - Température : ±0.1°C (typique)
 * - Humidité relative : ±1% RH (typique)
 * - Adresse I2C : 0x44
 * 
 * Le bus I2C doit être initialisé au préalable (par init_rtc() 
 * par exemple) et le handle passé à init_sht45().
 */

/**
 * @brief Résultat d'une mesure SHT45
 */
typedef struct {
    float temperature;  /**< Température en °C */
    float humidity;     /**< Humidité relative en % */
} sht45_data_t;

/**
 * @brief Initialise le capteur SHT45 sur un bus I2C existant
 * 
 * Ajoute le device SHT45 (adresse 0x44) sur le bus I2C fourni
 * et vérifie que le capteur répond.
 * 
 * @param[in] bus_handle Handle du bus I2C maître (déjà initialisé)
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t init_sht45(i2c_master_bus_handle_t bus_handle);

/**
 * @brief Lit la température et l'humidité depuis le SHT45
 * 
 * Lance une mesure haute précision (commande 0xFD), attend ~10 ms,
 * puis lit les 6 octets de résultat avec vérification CRC-8.
 * 
 * @param[out] data Structure remplie avec température (°C) et humidité (%)
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t read_sht45(sht45_data_t *data);

/**
 * @brief Libère le device SHT45 du bus I2C
 * 
 * Ne touche pas au bus I2C lui-même (géré par le module RTC).
 * 
 * @return ESP_OK en cas de succès
 */
esp_err_t deinit_sht45(void);

#endif // SHT45_H
