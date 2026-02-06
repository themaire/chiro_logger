idf_component_register(SRCS "main.c" "led_rgb.c" "sd_card.c" "battery.c" "rtc_clock.c"
                      INCLUDE_DIRS "."
                      REQUIRES driver esp_timer fatfs sdmmc spiffs esp_adc)
