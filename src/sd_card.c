#include "sd_card.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>

static const char *TAG = "SD_CARD";
static sdmmc_card_t *s_card = NULL;

// Configuration des pins pour le shield SD/RTC sur LOLIN C3 PICO
// MAPPING VÉRIFIÉ AU MULTIMÈTRE - D1 Mini shield → C3 PICO GPIOs:
// D5(CLK)→GPIO1, D6(DO/MISO)→GPIO0, D7(DI/MOSI)→GPIO4, D8(CS)→GPIO5
#define PIN_NUM_CLK  1   // D5 → GPIO1 - CLK  (Serial clock)
#define PIN_NUM_MISO 0   // D6 → GPIO0 - DO/MISO (Data Out from card)
#define PIN_NUM_MOSI 4   // D7 → GPIO4 - DI/MOSI (Data In to card)
#define PIN_NUM_CS   5   // D8 → GPIO5 - CS (Cable select)

// Point de montage de la carte SD
#define MOUNT_POINT "/sdcard"

esp_err_t init_sd_card(void)
{
    esp_err_t ret;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    // Initialiser le bus SPI si nécessaire (ignore ESP_ERR_INVALID_STATE si déjà init)
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ret = spi_bus_initialize(host.slot, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    gpio_set_pull_mode(PIN_NUM_MISO, GPIO_PULLUP_ONLY);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SD card mounted.");
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t unmount_sd_card(void)
{
    if (s_card == NULL) {
        return ESP_OK;
    }
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount SD card (%s)", esp_err_to_name(ret));
        return ret;
    }
    s_card = NULL;
    ESP_LOGI(TAG, "SD card unmounted.");
    return ESP_OK;
}

esp_err_t log_data_to_csv(const char* filepath, int id, const char* datetime, float temperature, float humidity)
{
    FILE *f = fopen(filepath, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        return ESP_FAIL;
    }
    fprintf(f, "%d,%s,%.2f,%.2f\n", id, datetime, temperature, humidity);
    fclose(f);
    ESP_LOGI(TAG, "Data logged to %s", filepath);
    return ESP_OK;
}
