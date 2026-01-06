#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <esp_err.h>
#include <esp_event.h>
#include <stdbool.h>

/**
 * 📡 BLE Manager - Module de gestion Bluetooth pour Chiro Logger
 * 
 * Ce module gère la communication BLE avec l'application PWA Angular
 * pour le transfert des données de température/humidité.
 * 
 * 🔋 OPTIMISATION ÉNERGÉTIQUE :
 * - BLE activé uniquement en mode transfert
 * - Arrêt automatique après timeout
 * - Pas d'impact sur l'autonomie en mode normal
 */

// Configuration BLE
#define BLE_DEVICE_NAME "ChiroLogger"
#define BLE_SERVICE_UUID "12345678-1234-1234-1234-123456789ABC"
#define BLE_CHAR_DATA_UUID "87654321-4321-4321-4321-CBA987654321"
#define BLE_CHAR_STATUS_UUID "11111111-2222-3333-4444-555555555555"

// Timeout du mode transfert (en secondes)
#define BLE_TRANSFER_TIMEOUT_SEC 300  // 5 minutes max

// États du BLE Manager
typedef enum {
    BLE_STATE_STOPPED,      // BLE arrêté (mode normal)
    BLE_STATE_ADVERTISING,  // BLE en publicité (attente connexion)
    BLE_STATE_CONNECTED,    // Client connecté
    BLE_STATE_TRANSFERRING  // Transfert de données en cours
} ble_state_t;

// Structure des statistiques de transfert
typedef struct {
    uint32_t bytes_sent;        // Octets envoyés
    uint32_t records_sent;      // Nombre de mesures envoyées
    uint32_t connection_time;   // Durée de connexion (secondes)
    bool transfer_completed;    // Transfert terminé avec succès
} ble_transfer_stats_t;

/**
 * 🚀 Initialiser le module BLE
 * 
 * @return ESP_OK si succès, erreur sinon
 */
esp_err_t ble_manager_init(void);

/**
 * 📡 Démarrer le mode transfert BLE
 * 
 * Active le BLE, démarre la publicité et attend une connexion
 * de l'application PWA pour transférer les données.
 * 
 * @return ESP_OK si succès, erreur sinon
 */
esp_err_t ble_manager_start_transfer_mode(void);

/**
 * ⏹️ Arrêter le BLE et retourner en mode normal
 * 
 * @return ESP_OK si succès, erreur sinon
 */
esp_err_t ble_manager_stop(void);

/**
 * 📊 Obtenir l'état actuel du BLE
 * 
 * @return État actuel du BLE Manager
 */
ble_state_t ble_manager_get_state(void);

/**
 * 📈 Obtenir les statistiques du dernier transfert
 * 
 * @param stats Pointeur vers la structure à remplir
 * @return ESP_OK si succès, erreur sinon
 */
esp_err_t ble_manager_get_transfer_stats(ble_transfer_stats_t *stats);

/**
 * 📤 Envoyer les données CSV via BLE
 * 
 * Cette fonction lit les données depuis le tampon flash et/ou la SD
 * et les transmet au client BLE connecté.
 * 
 * @return ESP_OK si succès, erreur sinon
 */
esp_err_t ble_manager_send_data(void);

/**
 * ⏱️ Vérifier le timeout du mode transfert
 * 
 * À appeler périodiquement pour arrêter automatiquement le BLE
 * si aucune activité n'est détectée.
 * 
 * @return true si timeout atteint, false sinon
 */
bool ble_manager_check_timeout(void);

/**
 * 🔧 Callback d'événements BLE (usage interne)
 */
void ble_manager_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data);

#endif // BLE_MANAGER_H
