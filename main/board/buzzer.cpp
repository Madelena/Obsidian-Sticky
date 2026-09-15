// =============================================================================
// BUZZER
// =============================================================================
// LEDC PWM tones on GPIO48.
#include "board/buzzer.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace buzzer {
namespace {

constexpr const char *kTag = "buzzer";
constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
// Quarter duty keeps the piezo audible without being shrill.
constexpr uint32_t kDuty = 256;

bool s_ready = false;
bool s_enabled = true;

// Sets the PWM duty and latches it.
void set_duty(uint32_t duty)
{
    ledc_set_duty(kMode, kChannel, duty);
    ledc_update_duty(kMode, kChannel);
}

}  // namespace


esp_err_t init()
{
    ledc_timer_config_t timer = {};
    timer.speed_mode = kMode;
    timer.timer_num = kTimer;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.freq_hz = 2400;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), kTag, "timer");

    ledc_channel_config_t channel = {};
    channel.gpio_num = PIN_BUZZER;
    channel.speed_mode = kMode;
    channel.channel = kChannel;
    channel.intr_type = LEDC_INTR_DISABLE;
    channel.timer_sel = kTimer;
    channel.duty = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), kTag, "channel");
    s_ready = true;
    return ESP_OK;
}


void set_enabled(bool enabled)
{
    s_enabled = enabled;
}


void beep(uint32_t frequency_hz, uint32_t duration_ms)
{
    if (!s_ready || !s_enabled || frequency_hz == 0) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }
    ledc_set_freq(kMode, kTimer, frequency_hz);
    set_duty(kDuty);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    set_duty(0);
}


void cue_start()
{
    beep(1800, 60);
    beep(2400, 80);
}


void cue_stop()
{
    beep(2000, 80);
}


void cue_saved()
{
    beep(2800, 60);
    vTaskDelay(pdMS_TO_TICKS(40));
    beep(2800, 60);
}


void cue_error()
{
    beep(900, 250);
}

}  // namespace buzzer
