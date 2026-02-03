#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/spi_master.h>
#include "sd_card.h"

#ifdef RUN_SD_TEST
// Boucle de test SD : monte/écrit/démonte toutes les 2s
void app_main(void)
{
    // Initialisation du bus SPI2 (HSPI) une seule fois
    spi_bus_config_t buscfg = {
        .mosi_io_num = 4, // GPIO4 (D7)
        .miso_io_num = 0, // GPIO0 (D6)
        .sclk_io_num = 1, // GPIO1 (D5)
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000
    };
    esp_err_t spi_ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (spi_ret != ESP_OK && spi_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("SD_TEST", "Erreur initialisation SPI2: %d", spi_ret);
        return;
    }

    while (1) {
        esp_err_t ret = init_sd_card();
        if (ret == ESP_OK) {
            ESP_LOGI("SD_TEST", "Carte SD montée avec succès");
            log_data_to_csv("/sdcard/test_log.csv", 1, "2026-02-03T12:00:00", 23.5, 45.2);
            esp_err_t unmount_ret = unmount_sd_card();
            if (unmount_ret != ESP_OK) {
                ESP_LOGE("SD_TEST", "Erreur démontage SD: %d", unmount_ret);
            }
        } else {
            ESP_LOGE("SD_TEST", "Erreur montage SD: %d", ret);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
#endif // RUN_SD_TEST
