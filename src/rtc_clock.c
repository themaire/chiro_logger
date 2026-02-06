#include "rtc_clock.h"
#include "config.h"
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "RTC_CLOCK";

// DS1307 : adresse I2C 0x68, registres temps en BCD (0x00-0x06)
#define DS1307_ADDR      0x68
#define DS1307_REG_TIME  0x00  // Premier registre (secondes)

// Handles I2C (internes au module, à partager avec SHT45 plus tard)
static i2c_master_bus_handle_t s_bus_handle = NULL;
static i2c_master_dev_handle_t s_dev_handle = NULL;

// ============================================================================
// Helpers BCD <-> Décimal
// ============================================================================

static uint8_t bcd_to_dec(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static uint8_t dec_to_bcd(uint8_t dec)
{
    return ((dec / 10) << 4) | (dec % 10);
}

// Calcul du jour de la semaine (algorithme de Tomohiko Sakamoto)
// Retourne 0=Dimanche, 1=Lundi, ..., 6=Samedi
static int calc_day_of_week(int y, int m, int d)
{
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

// ============================================================================
// API publique
// ============================================================================

esp_err_t init_rtc(void)
{
    ESP_LOGI(TAG, "🕐 Initialisation RTC DS1307 (I2C: SDA=GPIO%d, SCL=GPIO%d)...",
             I2C_SDA_PIN, I2C_SCL_PIN);

    // Configurer le bus I2C maître
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_PIN,
        .sda_io_num = I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ i2c_new_master_bus failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Ajouter le DS1307 sur le bus (adresse 0x68, 100 kHz)
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS1307_ADDR,
        .scl_speed_hz = 100000,
    };

    ret = i2c_master_bus_add_device(s_bus_handle, &dev_config, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ i2c_master_bus_add_device failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Vérifier que le DS1307 répond (lecture du registre 0)
    uint8_t reg = DS1307_REG_TIME;
    uint8_t data;
    ret = i2c_master_transmit_receive(s_dev_handle, &reg, 1, &data, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ DS1307 ne répond pas sur I2C (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Bit 7 du registre secondes = CH (Clock Halt)
    // Si CH=1, l'oscillateur est arrêté → l'heure n'est pas configurée
    if (data & 0x80) {
        ESP_LOGW(TAG, "⚠️  Oscillateur RTC arrêté - horloge non configurée");
    } else {
        ESP_LOGI(TAG, "✅ RTC DS1307 initialisé - oscillateur actif");
    }

    return ESP_OK;
}

esp_err_t rtc_get_time(rtc_time_t *time)
{
    if (s_dev_handle == NULL) {
        ESP_LOGE(TAG, "❌ RTC non initialisé, appeler init_rtc() d'abord");
        return ESP_ERR_INVALID_STATE;
    }
    if (time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Lire les 7 registres temps d'un coup (auto-incrémentation DS1307)
    uint8_t reg = DS1307_REG_TIME;
    uint8_t data[7];

    esp_err_t ret = i2c_master_transmit_receive(s_dev_handle, &reg, 1, data, 7, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Lecture RTC échouée (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Conversion BCD → décimal (masquer les bits de contrôle)
    time->seconds = bcd_to_dec(data[0] & 0x7F);  // Masquer bit CH
    time->minutes = bcd_to_dec(data[1] & 0x7F);
    time->hours   = bcd_to_dec(data[2] & 0x3F);  // Mode 24h
    time->day     = data[3] & 0x07;               // 1-7
    time->date    = bcd_to_dec(data[4] & 0x3F);
    time->month   = bcd_to_dec(data[5] & 0x1F);
    time->year    = 2000 + bcd_to_dec(data[6]);

    return ESP_OK;
}

esp_err_t rtc_set_time(const rtc_time_t *time)
{
    if (s_dev_handle == NULL) {
        ESP_LOGE(TAG, "❌ RTC non initialisé");
        return ESP_ERR_INVALID_STATE;
    }
    if (time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Écriture des 7 registres en une seule transaction I2C
    // [adresse_registre, secondes, minutes, heures, jour, date, mois, année]
    uint8_t data[8];
    data[0] = DS1307_REG_TIME;                     // Adresse du premier registre
    data[1] = dec_to_bcd(time->seconds) & 0x7F;   // CH=0 → oscillateur actif
    data[2] = dec_to_bcd(time->minutes);
    data[3] = dec_to_bcd(time->hours) & 0x3F;     // Mode 24h
    data[4] = time->day & 0x07;
    data[5] = dec_to_bcd(time->date);
    data[6] = dec_to_bcd(time->month);
    data[7] = dec_to_bcd(time->year - 2000);

    esp_err_t ret = i2c_master_transmit(s_dev_handle, data, 8, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Écriture RTC échouée (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "✅ Heure RTC programmée: %04d-%02d-%02d %02d:%02d:%02d",
             time->year, time->month, time->date,
             time->hours, time->minutes, time->seconds);

    return ESP_OK;
}

esp_err_t rtc_set_time_from_compile(void)
{
    // Macros du compilateur : __DATE__ = "Feb  6 2026", __TIME__ = "14:30:05"
    const char *compile_date = __DATE__;
    const char *compile_time = __TIME__;

    ESP_LOGI(TAG, "🕐 Programmation RTC depuis la compilation: %s %s",
             compile_date, compile_time);

    const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};

    rtc_time_t t = {0};

    // Parser le mois (3 premières lettres)
    char month_str[4] = {0};
    memcpy(month_str, compile_date, 3);
    for (int i = 0; i < 12; i++) {
        if (strcmp(month_str, months[i]) == 0) {
            t.month = i + 1;
            break;
        }
    }

    // Parser le jour et l'année
    int day = 0, year = 0;
    sscanf(compile_date + 4, "%d %d", &day, &year);
    t.date = (uint8_t)day;
    t.year = (uint16_t)year;

    // Parser l'heure
    int h = 0, m = 0, s = 0;
    sscanf(compile_time, "%d:%d:%d", &h, &m, &s);
    t.hours = (uint8_t)h;
    t.minutes = (uint8_t)m;
    t.seconds = (uint8_t)s;

    // Calculer le jour de la semaine (DS1307 : 1=Dim, 2=Lun, ..., 7=Sam)
    int dow = calc_day_of_week(t.year, t.month, t.date);
    t.day = (uint8_t)(dow + 1);  // Sakamoto retourne 0=Dim → +1 pour DS1307

    return rtc_set_time(&t);
}

void rtc_format_datetime(const rtc_time_t *time, char *buf, size_t buf_len)
{
    if (time == NULL || buf == NULL || buf_len == 0) {
        return;
    }
    snprintf(buf, buf_len, "%04d-%02d-%02d %02d:%02d:%02d",
             time->year, time->month, time->date,
             time->hours, time->minutes, time->seconds);
}

esp_err_t deinit_rtc(void)
{
    if (s_dev_handle != NULL) {
        i2c_master_bus_rm_device(s_dev_handle);
        s_dev_handle = NULL;
    }
    if (s_bus_handle != NULL) {
        i2c_del_master_bus(s_bus_handle);
        s_bus_handle = NULL;
    }
    ESP_LOGI(TAG, "🕐 RTC libéré");
    return ESP_OK;
}
