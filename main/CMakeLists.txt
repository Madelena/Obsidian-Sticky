# =============================================================================
# MAIN COMPONENT
# =============================================================================
# Every application source file must be listed here; a file missing from SRCS
# links cleanly and silently does nothing.
idf_component_register(
    SRCS
        "main.cpp"
        "board/board.cpp"
        "board/buzzer.cpp"
        "board/battery.cpp"
        "audio/pdm_mic.cpp"
        "audio/clip.cpp"
        "ui/font.cpp"
        "ui/text.cpp"
        "ui/canvas.cpp"
        "ui/display.cpp"
        "ui/screen.cpp"
        "net/wifi.cpp"
        "net/http.cpp"
        "net/stt_client.cpp"
        "net/llm_client.cpp"
        "net/obsidian_client.cpp"
        "portal/portal.cpp"
        "app/settings.cpp"
        "app/input.cpp"
        "app/power.cpp"
        "app/pipeline.cpp"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "portal/index.html"
    REQUIRES
        bq27220 button seeed_epaper dns_server
        esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_ledc esp_driver_spi
        esp_hw_support esp_timer esp_app_format nvs_flash json
        esp_wifi esp_netif esp_event esp_http_client esp_http_server esp-tls mbedtls
)
