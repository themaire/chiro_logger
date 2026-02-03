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
// 🎨 MODE VISUEL - Feedback LED RGB
// ============================================================================
// Active les signaux LED RGB pour debugging et tests terrain
// COMMENTER cette ligne pour désactiver les LEDs et économiser la batterie
#define VISUAL_MODE

// ============================================================================
// 📟 MODE PRODUCTION - Optimisation logs
// ============================================================================
// Active le mode production avec logs minimaux pour économie d'énergie
// DÉCOMMENTER cette ligne pour le déploiement terrain longue durée
// #define PRODUCTION_MODE

// ============================================================================
// ⏰ CONFIGURATION DEEP SLEEP
// ============================================================================
#define DEEP_SLEEP_DURATION_SEC 5  // Durée entre mesures (secondes)

// ============================================================================
// 💾 CONFIGURATION TAMPON FLASH
// ============================================================================
#define BUFFER_FLUSH_THRESHOLD 500  // Nombre de mesures avant flush SD
#define BUFFER_MOUNT_POINT "/buffer"
#define BUFFER_CSV_FILE "/buffer/data_buffer.csv"

// ============================================================================
// 🔘 CONFIGURATION BOUTON RÉVEIL
// ============================================================================
#define WAKEUP_BUTTON_PIN GPIO_NUM_2  // Bouton réveil BLE sur GPIO2 (compatible deep sleep)
#define WAKEUP_BUTTON_LEVEL 0         // Niveau bas = appuyé (pull-up)

#endif // CONFIG_H
