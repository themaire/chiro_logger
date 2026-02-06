#ifndef RTC_CLOCK_H
#define RTC_CLOCK_H

#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>
#include <driver/i2c_master.h>

/**
 * @file rtc_clock.h
 * @brief Driver RTC DS1307 via I2C pour horodatage des mesures
 * 
 * Ce module pilote le DS1307 présent sur le shield SD/RTC D1 Mini
 * via le bus I2C (SDA=GPIO8, SCL=GPIO10 sur LOLIN C3 Mini/PICO).
 * 
 * Le DS1307 dispose d'une pile de sauvegarde (CR2032) qui maintient
 * l'heure même lorsque l'alimentation principale est coupée.
 * 
 * Note: Le bus I2C sera partagé avec le capteur SHT45 (adresse 0x44).
 * Pour l'instant le bus est géré en interne par ce module.
 */

/**
 * @brief Structure représentant une date/heure complète
 */
typedef struct {
    uint8_t seconds;    /**< Secondes (0-59) */
    uint8_t minutes;    /**< Minutes (0-59) */
    uint8_t hours;      /**< Heures (0-23, format 24h) */
    uint8_t day;        /**< Jour de la semaine (1=Dim, 2=Lun, ..., 7=Sam) */
    uint8_t date;       /**< Jour du mois (1-31) */
    uint8_t month;      /**< Mois (1-12) */
    uint16_t year;      /**< Année complète (ex: 2026) */
} rtc_time_t;

/**
 * @brief Initialise le bus I2C et vérifie la présence du DS1307
 * 
 * Configure I2C0 avec les pins définis dans config.h et vérifie
 * que le DS1307 répond à l'adresse 0x68. Vérifie aussi si
 * l'oscillateur est actif (bit CH).
 * 
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t init_rtc(void);

/**
 * @brief Lit la date/heure courante depuis le DS1307
 * 
 * @param[out] time Structure remplie avec la date/heure lue
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t rtc_get_time(rtc_time_t *time);

/**
 * @brief Programme une date/heure dans le DS1307
 * 
 * Active automatiquement l'oscillateur (bit CH = 0).
 * 
 * @param[in] time Structure contenant la date/heure à programmer
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t rtc_set_time(const rtc_time_t *time);

/**
 * @brief Programme le DS1307 avec la date/heure de compilation
 * 
 * Utilise les macros __DATE__ et __TIME__ du compilateur.
 * Pratique pour l'initialisation initiale : compiler puis flasher
 * immédiatement, l'heure sera précise à ~10-30 secondes près.
 * 
 * @return ESP_OK en cas de succès, code d'erreur sinon
 */
esp_err_t rtc_set_time_from_compile(void);

/**
 * @brief Formate une date/heure en chaîne ISO "YYYY-MM-DD HH:MM:SS"
 * 
 * @param[in] time Structure date/heure à formater
 * @param[out] buf Buffer de sortie (minimum 20 caractères)
 * @param[in] buf_len Taille du buffer
 */
void rtc_format_datetime(const rtc_time_t *time, char *buf, size_t buf_len);

/**
 * @brief Retourne le handle du bus I2C maître
 * 
 * Permet aux autres modules (SHT45, etc.) de partager le bus I2C
 * initialisé par init_rtc(). Retourne NULL si le bus n'est pas initialisé.
 * 
 * @return Handle du bus I2C ou NULL
 */
i2c_master_bus_handle_t rtc_get_i2c_bus(void);

/**
 * @brief Libère les ressources I2C
 * 
 * @return ESP_OK en cas de succès
 */
esp_err_t deinit_rtc(void);

#endif // RTC_CLOCK_H
