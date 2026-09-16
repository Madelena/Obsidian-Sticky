// =============================================================================
// MAIN
// =============================================================================
// Boot order for the Obsidian Sticky firmware: latch power, bring up storage,
// display, audio, buttons, and networking, then hand over to pipeline.cpp.
#include "app/input.h"
#include "app/pipeline.h"
#include "app/settings.h"
#include "audio/clip.h"
#include "audio/pdm_mic.h"
#include "board/battery.h"
#include "board/board.h"
#include "board/buzzer.h"
#include "board/touch.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "net/wifi.h"
#include "nvs_flash.h"
#include "ui/canvas.h"
#include "ui/cjk_font.h"
#include "ui/display.h"
#include "ui/font.h"
#include "ui/screen.h"

namespace {

constexpr const char *kTag = "main";

// Initializes NVS, erasing it once when the layout changed after an update.
esp_err_t init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), kTag, "nvs erase");
        err = nvs_flash_init();
    }
    return err;
}

}  // namespace


extern "C" void app_main()
{
    // The latch comes first: everything below takes longer than the rail
    // stays up on its own.
    ESP_ERROR_CHECK(board::init());
    ESP_LOGI(kTag, "Obsidian Sticky v%s, button wake=%d", esp_app_get_description()->version,
             board::woke_from_button());

    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(settings::init());
    buzzer::init();
    buzzer::set_enabled(settings::get().beep);
    // Before anything measures or draws text, because the faces differ in
    // height and a wrap done under the wrong family would be laid out wrong.
    font::set_family(font::family_from_name(settings::get().text_font.c_str()));

    // Mic before display so a wake-and-hold press loses as little as possible.
    ESP_ERROR_CHECK(clip::init());
    ESP_ERROR_CHECK(input::init());
    if (pdm_mic::init() != ESP_OK) {
        ESP_LOGE(kTag, "Microphone init failed");
    }

    ESP_ERROR_CHECK(canvas::init());
    // A missing or empty font partition only costs non-Latin glyphs, so this
    // result never stops the boot.
    cjk_font::init();
    ESP_ERROR_CHECK(display::init());
    if (battery::init(board::sensor_i2c_bus()) != ESP_OK) {
        ESP_LOGW(kTag, "Battery gauge unavailable");
    }
    // Creates the task only; pipeline.cpp powers the panel when a note that
    // overflows the screen is actually on it.
    ESP_ERROR_CHECK(touch::init());
    ESP_ERROR_CHECK(wifi::init());

    ESP_ERROR_CHECK(pipeline::start());
}
