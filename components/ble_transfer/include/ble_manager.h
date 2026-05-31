#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <esp_err.h>
#include <stdbool.h>

/**
 * 📡 BLE Manager — Stack NimBLE (ESP32-C3 / ESP-IDF 5.x)
 *
 * Gère la communication BLE avec l'application PWA Angular pour
 * le transfert des données CSV (température / humidité / batterie).
 *
 * Flux côté Angular :
 *   1. requestDevice() avec optionalServices:[serviceUUID]
 *   2. gatt.connect() → getPrimaryService() → getCharacteristic(DATA)
 *   3. startNotifications() + addEventListener('characteristicvaluechanged')
 *   4. readValue()  ← déclenche l'envoi des données en NOTIFY sur l'ESP32
 *
 * 🔋 Activation uniquement à la demande (bouton), arrêt automatique après timeout.
 */

// ── Identifiants GATT ────────────────────────────────────────────────────────
#define BLE_DEVICE_NAME          "ChiroLogger"
#define BLE_TRANSFER_TIMEOUT_SEC  300   // 5 minutes max

// ── États du BLE Manager ─────────────────────────────────────────────────────
typedef enum {
    BLE_STATE_STOPPED,       // BLE arrêté (mode normal)
    BLE_STATE_ADVERTISING,   // En publicité — attente connexion
    BLE_STATE_CONNECTED,     // Client connecté
    BLE_STATE_TRANSFERRING,  // Transfert de données en cours
} ble_state_t;

// ── API publique ──────────────────────────────────────────────────────────────

/**
 * Initialiser le stack NimBLE et enregistrer les services GATT.
 * Doit être appelé AVANT start_transfer_mode().
 */
esp_err_t ble_manager_init(void);

/**
 * Démarrer la publicité BLE et attendre une connexion PWA.
 * Bloque jusqu'à la fin du transfert ou jusqu'au timeout.
 * Appeler ble_manager_stop() pour forcer l'arrêt.
 */
esp_err_t ble_manager_start_transfer_mode(void);

/**
 * Arrêter le BLE et libérer les ressources.
 */
esp_err_t ble_manager_stop(void);

/**
 * Obtenir l'état courant du BLE Manager.
 */
ble_state_t ble_manager_get_state(void);

/**
 * Envoyer les données CSV depuis le tampon SPIFFS via NOTIFY BLE.
 * Appelé en interne depuis le callback GATT READ.
 * Thread-safe : peut être appelé depuis n'importe quelle tâche FreeRTOS.
 */
esp_err_t ble_manager_send_data(void);

#endif // BLE_MANAGER_H
