#include "sht45.h"
#include "config.h"
#include "settings.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "SHT45";

// SHT45 : adresse I2C 0x44
#define SHT45_ADDR              0x44

// Commandes SHT45
#define SHT45_CMD_MEASURE_HP    0xFD  // Mesure haute précision (sans chauffage)
#define SHT45_CMD_SERIAL        0x89  // Lire le numéro de série
#define SHT45_CMD_SOFT_RESET    0x94  // Soft reset

// Temps d'attente après commande de mesure haute précision
// Datasheet: max 8.3 ms, on prend large pour éviter les NACK
#define SHT45_MEASURE_DELAY_MS  20

// Nombre de tentatives de lecture (le SHT45 peut NACK si pas encore prêt)
#define SHT45_READ_RETRIES      3
#define SHT45_RETRY_DELAY_MS    10

// Handle du device I2C SHT45
static i2c_master_dev_handle_t s_dev_handle = NULL;

// ============================================================================
// CRC-8 (polynôme 0x31, init 0xFF) - Spécification Sensirion
// ============================================================================
static uint8_t sht45_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

// ============================================================================
// API publique
// ============================================================================

esp_err_t init_sht45(i2c_master_bus_handle_t bus_handle)
{
    ESP_LOGI(TAG, "🌡️  Initialisation capteur SHT45 (I2C addr 0x%02X)...", SHT45_ADDR);

    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "❌ Handle bus I2C invalide (NULL)");
        return ESP_ERR_INVALID_ARG;
    }

    // Ajouter le SHT45 sur le bus I2C existant
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT45_ADDR,
        .scl_speed_hz = 100000,  // 100 kHz (compatible DS1307)
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_config, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Impossible d'ajouter SHT45 sur le bus I2C (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Soft reset pour s'assurer que le capteur est dans un état connu
    uint8_t cmd = SHT45_CMD_SOFT_RESET;
    ret = i2c_master_transmit(s_dev_handle, &cmd, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ SHT45 ne répond pas sur I2C (%s)", esp_err_to_name(ret));
        i2c_master_bus_rm_device(s_dev_handle);
        s_dev_handle = NULL;
        return ret;
    }

    // Attendre après le soft reset (datasheet: 1 ms min, on prend 10 ms de marge)
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "✅ Capteur SHT45 initialisé");
    return ESP_OK;
}

esp_err_t read_sht45(sht45_data_t *data)
{
    if (s_dev_handle == NULL) {
        ESP_LOGE(TAG, "❌ SHT45 non initialisé, appeler init_sht45() d'abord");
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Envoyer la commande de mesure haute précision
    uint8_t cmd = SHT45_CMD_MEASURE_HP;
    esp_err_t ret = i2c_master_transmit(s_dev_handle, &cmd, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Envoi commande mesure SHT45 échoué (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Attendre la fin de la mesure
    vTaskDelay(pdMS_TO_TICKS(SHT45_MEASURE_DELAY_MS));

    // Lire les 6 octets de résultat avec retry (NACK possible si pas encore prêt) :
    // [temp_MSB, temp_LSB, temp_CRC, hum_MSB, hum_LSB, hum_CRC]
    uint8_t raw[6];
    ret = ESP_FAIL;
    for (int attempt = 0; attempt < SHT45_READ_RETRIES; attempt++) {
        ret = i2c_master_receive(s_dev_handle, raw, 6, 100);
        if (ret == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "⏳ Tentative %d/%d - SHT45 pas encore prêt, attente...",
                 attempt + 1, SHT45_READ_RETRIES);
        vTaskDelay(pdMS_TO_TICKS(SHT45_RETRY_DELAY_MS));
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Lecture mesure SHT45 échouée (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Vérifier les CRC
    if (sht45_crc8(&raw[0], 2) != raw[2]) {
        ESP_LOGE(TAG, "❌ CRC température invalide");
        return ESP_ERR_INVALID_CRC;
    }
    if (sht45_crc8(&raw[3], 2) != raw[5]) {
        ESP_LOGE(TAG, "❌ CRC humidité invalide");
        return ESP_ERR_INVALID_CRC;
    }

    // Conversion des valeurs brutes en grandeurs physiques
    // Formules Sensirion (datasheet SHT4x)
    uint16_t raw_temp = (raw[0] << 8) | raw[1];
    uint16_t raw_hum  = (raw[3] << 8) | raw[4];

    data->temperature = -45.0f + 175.0f * ((float)raw_temp / 65535.0f) + g_settings.sht45_temp_offset;
    data->humidity    =  -6.0f + 125.0f * ((float)raw_hum  / 65535.0f) + g_settings.sht45_humidity_offset;

    // Clamp humidité dans la plage valide [0, 100]
    if (data->humidity < 0.0f)   data->humidity = 0.0f;
    if (data->humidity > 100.0f) data->humidity = 100.0f;

    ESP_LOGI(TAG, "🌡️  SHT45: T=%.2f°C, H=%.2f%%", data->temperature, data->humidity);
    return ESP_OK;
}

esp_err_t deinit_sht45(void)
{
    if (s_dev_handle != NULL) {
        i2c_master_bus_rm_device(s_dev_handle);
        s_dev_handle = NULL;
    }
    ESP_LOGI(TAG, "🌡️  SHT45 libéré");
    return ESP_OK;
}
