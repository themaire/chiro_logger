#ifndef CONFIG_H
#define CONFIG_H

/**
 * @file config.h
 * @brief Configuration globale du Chiro Logger
 * 
 * Ce fichier centralise tous les paramètres de configuration
 * partagés entre les différents modules du projet.
 */

// ============================================================================
// 🧪 MODE TEST SHT45 - Lecture sonde en boucle sur moniteur série
// ============================================================================
// Active un app_main simplifié qui lit le SHT45 toutes les 2 secondes
// et affiche les valeurs sur le moniteur série.
// Pas besoin du shield RTC/SD, juste la sonde sur le bus I2C.
// COMMENTER cette ligne pour revenir au mode normal
// #define TEST_SHT45

// ============================================================================
// 🎨 MODE VISUEL - Feedback LED RGB
// ============================================================================
// Active les signaux LED RGB pour debugging et tests terrain
// COMMENTER cette ligne pour désactiver les LEDs et économiser la batterie
// #define VISUAL_MODE

// ============================================================================
// 📟 MODE PRODUCTION - Optimisation logs
// ============================================================================
// Active le mode production avec logs minimaux pour économie d'énergie
// DÉCOMMENTER cette ligne pour le déploiement terrain longue durée
// #define PRODUCTION_MODE

// ============================================================================
// ⏰ CONFIGURATION DEEP SLEEP
// ============================================================================
// #define DEEP_SLEEP_DURATION_SEC 60  // Durée entre mesures (secondes)
#define DEEP_SLEEP_DURATION_SEC 1800  // 30 minutes = 30 minutes entre mesures (pour tests terrain plus rapides)

// ============================================================================
// 💾 CONFIGURATION TAMPON FLASH
// ============================================================================
#define BUFFER_FLUSH_THRESHOLD 200  // Nombre de mesures avant flush SD
#define BUFFER_MOUNT_POINT "/buffer"
#define BUFFER_CSV_FILE "/buffer/data_buffer.csv"

// ============================================================================
// 🔘 CONFIGURATION BOUTON RÉVEIL
// ============================================================================
#define WAKEUP_BUTTON_PIN GPIO_NUM_2  // Bouton réveil BLE sur GPIO2 (compatible deep sleep)
#define WAKEUP_BUTTON_LEVEL 0         // Niveau bas = appuyé (pull-up)

// ============================================================================
// 🕐 CONFIGURATION I2C - RTC DS1307 + Capteur SHT45
// ============================================================================
// D1 Mini shield → C3 Mini/PICO : D2(SDA)→GPIO8, D1(SCL)→GPIO10
#define I2C_SDA_PIN  8   // D2 → GPIO8  - SDA (données I2C)
#define I2C_SCL_PIN  10  // D1 → GPIO10 - SCL (horloge I2C)

// ============================================================================
// 🌡️  CALIBRATION SHT45
// ============================================================================
// Offsets ajoutés aux valeurs brutes du capteur pour compenser les écarts
// Valeur finale = valeur lue + offset
// Exemple : si la sonde lit 21.75°C et le thermomètre de référence 19.8°C → offset = -1.95
#define SHT45_TEMP_OFFSET     -0.5f  // Correction température en °C
#define SHT45_HUMIDITY_OFFSET  0.0f   // Correction humidité en %RH

#endif // CONFIG_H
