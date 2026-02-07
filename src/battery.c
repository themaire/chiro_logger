#include "battery.h"
#include <esp_log.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

static const char *TAG = "BATTERY";

// GPIO3 = ADC1_CHANNEL_3 sur ESP32-C3
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_3

// Le LOLIN C3 Mini utilise un diviseur de tension 2:1 (100K/100K)
// L'ADC lit donc la moitié de la tension réelle de la batterie
#define VOLTAGE_DIVIDER_RATIO 2.0f

// Nombre de lectures pour moyenner (stabilité)
#define ADC_SAMPLES 16

// Seuils LiPo calibrés (batterie LiPo 3000mAh, chargeur intégré LOLIN C3 PICO)
// Tension max mesurée à pleine charge via ADC : 4.15V
#define BATTERY_FULL_V    4.18f  // 100%
#define BATTERY_EMPTY_V   3.0f   // 0%

// Handles ADC
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_calibrated = false;

// Initialise la calibration ADC (curve fitting sur ESP32-C3)
static esp_err_t init_adc_calibration(void)
{
    esp_err_t ret = ESP_FAIL;

    #if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✅ Calibration ADC (curve fitting) activée");
        }
    #elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &s_cali_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✅ Calibration ADC (line fitting) activée");
        }
    #endif

        s_calibrated = (ret == ESP_OK);
        if (!s_calibrated) {
            ESP_LOGW(TAG, "⚠️  Calibration ADC indisponible, lectures brutes utilisées");
        }
        return ret;
}

esp_err_t init_battery(void)
{
    ESP_LOGI(TAG, "🔋 Initialisation ADC batterie sur GPIO3...");

    // Configurer l'unité ADC1
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ adc_oneshot_new_unit failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Configurer le channel 3 (GPIO3) avec atténuation 12dB (~0-3.1V)
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ adc_oneshot_config_channel failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Tenter la calibration (non bloquant si échoue)
    init_adc_calibration();

    ESP_LOGI(TAG, "🔋 ADC batterie initialisé (GPIO3, ADC1_CH3, 12dB)");
    return ESP_OK;
}

esp_err_t read_battery(battery_info_t *info)
{
    if (s_adc_handle == NULL) {
        ESP_LOGE(TAG, "❌ ADC non initialisé, appeler init_battery() d'abord");
        return ESP_ERR_INVALID_STATE;
    }
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Moyenner plusieurs lectures pour stabiliser
    int raw_sum = 0;
    int voltage_mv_sum = 0;

    for (int i = 0; i < ADC_SAMPLES; i++) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(s_adc_handle, BATTERY_ADC_CHANNEL, &raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ Lecture ADC échouée (%s)", esp_err_to_name(ret));
            return ret;
        }
        raw_sum += raw;

        if (s_calibrated) {
            int mv = 0;
            adc_cali_raw_to_voltage(s_cali_handle, raw, &mv);
            voltage_mv_sum += mv;
        }
    }

    float voltage_at_adc;
    if (s_calibrated) {
        // Tension calibrée en mV → V
        voltage_at_adc = (float)(voltage_mv_sum / ADC_SAMPLES) / 1000.0f;
    } else {
        // Conversion brute : 12 bits (4095) ≈ 3.1V avec atténuation 12dB
        float raw_avg = (float)raw_sum / ADC_SAMPLES;
        voltage_at_adc = (raw_avg / 4095.0f) * 3.1f;
    }

    // Appliquer le facteur du diviseur de tension
    info->voltage = voltage_at_adc * VOLTAGE_DIVIDER_RATIO;

    // Calculer le pourcentage (linéaire entre EMPTY et FULL)
    if (info->voltage >= BATTERY_FULL_V) {
        info->percentage = 100;
    } else if (info->voltage <= BATTERY_EMPTY_V) {
        info->percentage = 0;
    } else {
        info->percentage = (int)(((info->voltage - BATTERY_EMPTY_V) / 
                                   (BATTERY_FULL_V - BATTERY_EMPTY_V)) * 100.0f);
    }

    return ESP_OK;
}

esp_err_t deinit_battery(void)
{
    if (s_cali_handle != NULL) {
        #if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
                adc_cali_delete_scheme_curve_fitting(s_cali_handle);
        #elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
                adc_cali_delete_scheme_line_fitting(s_cali_handle);
        #endif
                s_cali_handle = NULL;
                s_calibrated = false;
    }

    if (s_adc_handle != NULL) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
    }

    ESP_LOGI(TAG, "🔋 ADC batterie libéré");
    return ESP_OK;
}
