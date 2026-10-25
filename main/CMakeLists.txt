idf_component_register(
    SRCS "fortune_app.c" "fortune_almanac.c" "fortune_font.c" "fortune_font_12.c" "fortune_title_font.c" "fortune_logic.c" "fortune_qr.c" "fortune_net.c" "fortune_portal.c" "fortune_portal_form.c" "fortune_sound.c"
         "vendor/qrcodegen/qrcodegen.c"
    INCLUDE_DIRS "."
    REQUIRES app_update bsp esp_event esp_http_client esp_http_server esp_hw_support esp_netif esp_timer esp_wifi json nvs_flash
)
