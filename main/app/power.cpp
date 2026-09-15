// =============================================================================
// POWER
// =============================================================================
// Idle timer and the deep-sleep pin-hold sequence from Seeed's demo.
#include "app/power.h"

#include <cstdio>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/wifi.h"
#include "pin_config.h"

namespace power {
namespace {

constexpr const char *kTag = "power";
int64_t s_last_activity_us = 0;

// Drives a pin to level and latches it through deep sleep.
void hold_output(int pin, int level)
{
    const auto gpio = static_cast<gpio_num_t>(pin);
    gpio_hold_dis(gpio);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio, level);
    gpio_hold_en(gpio);
}

}  // namespace


void note_activity()
{
    s_last_activity_us = esp_timer_get_time();
}


bool idle_expired(int idle_minutes)
{
    if (idle_minutes <= 0) {
        return false;
    }
    return esp_timer_get_time() - s_last_activity_us > static_cast<int64_t>(idle_minutes) * 60 * 1000000;
}


[[noreturn]] void deep_sleep()
{
    wifi::stop();

    // The button must be up before it can be a wake source.
    while (gpio_get_level(static_cast<gpio_num_t>(PIN_POWER_BTN)) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(60));

    hold_output(PIN_POWER_HOLD, 1);
    hold_output(PIN_POWER_LOCK, 1);
    hold_output(PIN_EPD_EN, 0);
    hold_output(PIN_TOUCH_EN, 0);
    hold_output(PIN_TOUCH_RST, 0);
    hold_output(PIN_MIC_EN, 0);
    hold_output(PIN_SD_EN, 0);
    hold_output(PIN_BUZZER, 0);

    const auto wake = static_cast<gpio_num_t>(PIN_POWER_BTN);
    gpio_hold_dis(wake);
    gpio_set_direction(wake, GPIO_MODE_INPUT);
    gpio_pullup_en(wake);
    gpio_pulldown_dis(wake);
    gpio_hold_en(wake);
    esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_POWER_BTN, ESP_EXT1_WAKEUP_ANY_LOW);
    gpio_deep_sleep_hold_en();

    ESP_LOGI(kTag, "Deep sleep, wake on GPIO%d low", PIN_POWER_BTN);
    std::fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_deep_sleep_start();
}

}  // namespace power
