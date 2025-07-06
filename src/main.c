#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>

static const char *TAG = "CHIRO_LOGGER";

void app_main(void)
{
    ESP_LOGI(TAG, "🦇 Chiro Logger - Datalogger pour chiroptères");
    ESP_LOGI(TAG, "Version: 1.0.0");
    ESP_LOGI(TAG, "Plateforme: LOLIN D32 PRO (ESP32)");
    
    // Configuration initiale
    ESP_LOGI(TAG, "Initialisation du système...");
    
    // Boucle principale
    while (1) {
        ESP_LOGI(TAG, "Système en fonctionnement");
        vTaskDelay(pdMS_TO_TICKS(5000)); // Attendre 5 secondes
    }
}
