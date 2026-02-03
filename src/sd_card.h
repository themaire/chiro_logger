#ifndef SD_CARD_H
#define SD_CARD_H

#include <esp_err.h>

/**
 * @file sd_card.h
 * @brief Gestion de la carte SD pour stockage des données
 * 
 * Ce module fournit des fonctions pour initialiser, écrire et démonter
 * proprement une carte microSD via interface SPI.
 */

/**
 * @brief Initialise et monte la carte microSD
 * 
 * Configure le bus SPI et monte le système de fichiers FAT.
 * Crée automatiquement le répertoire de travail /sdcard/CHIRO.
 * 
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t init_sd_card(void);

/**
 * @brief Test d'écriture/lecture sur la carte SD
 * 
 * Crée un fichier de test, écrit des données et les relit pour vérifier
 * le bon fonctionnement de la carte SD.
 * 
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t test_sd_card(void);

/**
 * @brief Démonte proprement la carte SD
 * 
 * Démonte le système de fichiers FAT et libère les ressources.
 * Important pour éviter la corruption de données.
 * 
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t unmount_sd_card(void);

/**
 * @brief Enregistre des données au format CSV avec ID unique
 * 
 * @param filepath Chemin complet du fichier CSV (NULL = /sdcard/CHIRO/data.csv)
 * @param id Identifiant unique de la mesure (numéro de cycle)
 * @param datetime Timestamp de la mesure (NULL = génère timestamp automatique)
 * @param temperature Température en °C (-999.0 = N/A)
 * @param humidity Humidité en % (-999.0 = N/A)
 * 
 * @return ESP_OK en cas de succès, ESP_FAIL sinon
 * 
 * Note: Crée automatiquement l'en-tête CSV si le fichier n'existe pas
 */
esp_err_t log_data_to_csv(const char* filepath, int id, const char* datetime, 
                          float temperature, float humidity);

#endif // SD_CARD_H
