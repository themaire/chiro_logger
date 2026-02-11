#include "led_rgb.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/rmt_tx.h>
#include <driver/gpio.h>
#include <esp_log.h>

static const char *TAG = "LED_RGB";

// Configuration LED RGB WS2812
#define LED_RGB_PIN GPIO_NUM_7  // LED WS2812 intégrée sur LOLIN C3 PICO
#define LED_RGB_COUNT 1         // Nombre de LEDs dans la chaîne

// Timings WS2812 (en nanosecondes)
#define WS2812_T0H_NS 350
#define WS2812_T0L_NS 800
#define WS2812_T1H_NS 700
#define WS2812_T1L_NS 600
#define WS2812_RESET_US 280

// Handle RMT pour la LED RGB WS2812
static rmt_channel_handle_t led_chan = NULL;

/**
 * @brief Envoie une couleur RGB à la LED WS2812
 * 
 * @param r Composante rouge (0-255)
 * @param g Composante verte (0-255)
 * @param b Composante bleue (0-255)
 */
static void ws2812_send_pixel(uint8_t r, uint8_t g, uint8_t b) {
    if (led_chan == NULL) return;
    
    // WS2812 utilise l'ordre GRB, mais cette LED semble avoir R et B inversés
    uint32_t grb = (g << 16) | (b << 8) | r;
    
    // Créer les symboles RMT (24 bits pour GRB)
    rmt_symbol_word_t led_data[24];
    uint32_t resolution = 10000000;  // 10 MHz
    
    // Calculer les durées en ticks RMT
    uint32_t t0h = (WS2812_T0H_NS * resolution) / 1000000000;
    uint32_t t0l = (WS2812_T0L_NS * resolution) / 1000000000;
    uint32_t t1h = (WS2812_T1H_NS * resolution) / 1000000000;
    uint32_t t1l = (WS2812_T1L_NS * resolution) / 1000000000;
    
    // Encoder les 24 bits
    for (int i = 0; i < 24; i++) {
        uint32_t bit = (grb >> (23 - i)) & 0x01;
        if (bit) {
            led_data[i].level0 = 1;
            led_data[i].duration0 = t1h;
            led_data[i].level1 = 0;
            led_data[i].duration1 = t1l;
        } else {
            led_data[i].level0 = 1;
            led_data[i].duration0 = t0h;
            led_data[i].level1 = 0;
            led_data[i].duration1 = t0l;
        }
    }
    
    // Transmettre les données avec encodeur copy
    rmt_copy_encoder_config_t copy_encoder_config = {};
    rmt_encoder_handle_t copy_encoder = NULL;
    rmt_new_copy_encoder(&copy_encoder_config, &copy_encoder);
    
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    
    rmt_transmit(led_chan, copy_encoder, led_data, sizeof(led_data), &tx_config);
    rmt_tx_wait_all_done(led_chan, pdMS_TO_TICKS(100));
    
    // Nettoyer l'encodeur
    rmt_del_encoder(copy_encoder);
    
    // Délai de reset WS2812 (>50µs)
    vTaskDelay(pdMS_TO_TICKS(1));
}

esp_err_t init_led_rgb(void) {
    // TOUJOURS libérer le verrouillage GPIO du deep sleep précédent
    // La LED WS2812 est alimentée en 3.3V en permanence, donc le GPIO7
    // doit être configuré en sortie LOW même sans VISUAL_MODE
    gpio_hold_dis(LED_RGB_PIN);

    // Toujours initialiser le RMT : même sans VISUAL_MODE, on a besoin
    // du canal pour le flash bleu de démarrage (vérification batterie)
    // et les signaux d'urgence (force_display = true)
    ESP_LOGI(TAG, "Initialisation LED RGB WS2812 sur GPIO%d", LED_RGB_PIN);
    
    // Configuration du canal RMT
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_RGB_PIN,
        .mem_block_symbols = 64,
        .resolution_hz = 10000000,  // 10 MHz
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    
    esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &led_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur création canal RMT: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = rmt_enable(led_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur activation RMT: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LED RGB initialisée avec succès");
    return ESP_OK;
}

void set_led_rgb(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms, uint8_t blink_count, uint32_t blink_period_ms, bool force_display) {
#ifndef VISUAL_MODE
    // Mode terrain : LED désactivées pour économie batterie
    // SAUF si force_display = true (erreurs critiques)
    if (!force_display) {
        return;
    }
#endif
    
    if (led_chan == NULL) {
        ESP_LOGE(TAG, "LED RGB non initialisée");
        return;
    }
    
    if (blink_count == 0) {
        // Allumage fixe
        ws2812_send_pixel(r, g, b);
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        ws2812_send_pixel(0, 0, 0);  // Éteindre
    } else {
        // Mode clignotement
        uint32_t on_time = blink_period_ms / 2;
        uint32_t off_time = blink_period_ms - on_time;
        
        for (int i = 0; i < blink_count; i++) {
            // Allumer
            ws2812_send_pixel(r, g, b);
            vTaskDelay(pdMS_TO_TICKS(on_time));
            
            // Éteindre
            ws2812_send_pixel(0, 0, 0);
            if (i < blink_count - 1) {  // Pas de délai après le dernier clignotement
                vTaskDelay(pdMS_TO_TICKS(off_time));
            }
        }
    }
}

void led_off(void) {
    // Toujours permettre d'éteindre la LED (même en mode terrain)
    // car cela peut être nécessaire pour les signalisations d'urgence
    if (led_chan != NULL) {
        ws2812_send_pixel(0, 0, 0);
    }
}

void deinit_led_rgb(void) {
    // Éteindre la LED, libérer le canal RMT, et VERROUILLER le GPIO bas
    // INDISPENSABLE avant deep sleep sinon le GPIO flotte et la LED WS2812 reste allumée
    // car le WS2812 reste alimenté en 3.3V pendant le deep sleep
    if (led_chan != NULL) {
        // Envoyer plusieurs trames noires avec délais généreux pour être certain
        // que le WS2812 latch bien la couleur noire
        for (int i = 0; i < 5; i++) {
            ws2812_send_pixel(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        // Attendre le temps de reset WS2812 (>280µs) avant de toucher au GPIO
        vTaskDelay(pdMS_TO_TICKS(1));

        rmt_disable(led_chan);             // Désactiver le canal RMT
        rmt_del_channel(led_chan);         // Libérer le canal RMT
        led_chan = NULL;
    }

    // ⚠️ NE PAS utiliser gpio_reset_pin() ici !
    // gpio_reset_pin() active brièvement le pullup interne du GPIO7.
    // Cette impulsion HIGH (~1-10µs) sur la ligne de données WS2812 peut être
    // interprétée comme un ou plusieurs bits valides, causant un allumage aléatoire
    // de la LED (souvent bleu) pendant le deep sleep.
    // Bug intermittent : ~1 fois sur 6-12 cycles.
    //
    // Solution : gpio_config() avec pulldown activé pour une transition propre.
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_RGB_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   // Maintient la ligne basse pendant la transition
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_RGB_PIN, 0);

    // Attendre >280µs pour que le WS2812 latch la trame noire finale
    vTaskDelay(pdMS_TO_TICKS(1));

    // VERROUILLER l'état LOW pendant le deep sleep
    // Sans ça, le GPIO flotte quand le CPU s'éteint et le WS2812 reçoit du bruit
    gpio_hold_en(LED_RGB_PIN);
    ESP_LOGI("LED_RGB", "LED éteinte et GPIO%d verrouillé LOW pour deep sleep", LED_RGB_PIN);
}
