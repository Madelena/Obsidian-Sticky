// =============================================================================
// BOARD
// =============================================================================
// Power latch, shared buses, and safe pin states for the reTerminal Sticky.
// Sequence follows Seeed's dashboard demo; power.cpp relies on the held-pin
// list here matching what it holds before deep sleep.
#include "board/board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace board {
namespace {

constexpr const char *kTag = "board";
i2c_master_bus_handle_t s_sensor_bus = nullptr;

// Pins that power.cpp may leave held through deep sleep.
constexpr gpio_num_t kPossiblyHeldPins[] = {
    static_cast<gpio_num_t>(PIN_POWER_HOLD), static_cast<gpio_num_t>(PIN_POWER_LOCK),
    static_cast<gpio_num_t>(PIN_POWER_BTN),  static_cast<gpio_num_t>(PIN_EPD_EN),
    static_cast<gpio_num_t>(PIN_TOUCH_EN),   static_cast<gpio_num_t>(PIN_TOUCH_RST),
    static_cast<gpio_num_t>(PIN_TOUCH_INT),  static_cast<gpio_num_t>(PIN_MIC_EN),
    static_cast<gpio_num_t>(PIN_SD_EN),      static_cast<gpio_num_t>(PIN_BUZZER),
};

// Configures one push-pull output and sets its level.
esp_err_t configure_output(int pin, int level)
{
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << pin;
    config.mode = GPIO_MODE_OUTPUT;
    esp_err_t err = gpio_config(&config);
    if (err == ESP_OK) {
        err = gpio_set_level(static_cast<gpio_num_t>(pin), level);
    }
    return err;
}

// Pulses POWER_LOCK so the latch samples the current POWER_HOLD level.
void pulse_power_lock()
{
    gpio_set_level(static_cast<gpio_num_t>(PIN_POWER_LOCK), 0);
    esp_rom_delay_us(10);
    gpio_set_level(static_cast<gpio_num_t>(PIN_POWER_LOCK), 1);
    esp_rom_delay_us(10);
    gpio_set_level(static_cast<gpio_num_t>(PIN_POWER_LOCK), 0);
}

}  // namespace


bool woke_from_button()
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
}


bool woke_from_power()
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
}


esp_err_t init()
{
    // After deep sleep the latch pins are still held high; preload the same
    // levels before releasing the holds so the rail never blinks.
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED) {
        gpio_set_direction(static_cast<gpio_num_t>(PIN_POWER_HOLD), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(PIN_POWER_HOLD), 1);
        gpio_set_direction(static_cast<gpio_num_t>(PIN_POWER_LOCK), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(PIN_POWER_LOCK), 1);
    }
    gpio_deep_sleep_hold_dis();
    for (gpio_num_t pin : kPossiblyHeldPins) {
        gpio_hold_dis(pin);
    }

    ESP_RETURN_ON_ERROR(configure_output(PIN_POWER_HOLD, 1), kTag, "power hold");
    ESP_RETURN_ON_ERROR(configure_output(PIN_POWER_LOCK, 0), kTag, "power lock");
    pulse_power_lock();
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    // Park the SD card and touch controller: a floating chip select corrupts
    // panel transfers, and board/touch.cpp expects to find the panel off and
    // to raise PIN_TOUCH_EN itself when a note needs scrolling.
    ESP_RETURN_ON_ERROR(configure_output(PIN_SD_CS, 1), kTag, "sd cs");
    ESP_RETURN_ON_ERROR(configure_output(PIN_SD_EN, 1), kTag, "sd en");
    ESP_RETURN_ON_ERROR(configure_output(PIN_TOUCH_EN, 0), kTag, "touch en");
    ESP_RETURN_ON_ERROR(configure_output(PIN_TOUCH_RST, 0), kTag, "touch rst");

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_1;
    bus_config.sda_io_num = static_cast<gpio_num_t>(PIN_SENSOR_SDA);
    bus_config.scl_io_num = static_cast<gpio_num_t>(PIN_SENSOR_SCL);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = 1;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_sensor_bus), kTag, "sensor i2c");

    ESP_LOGI(kTag, "Power latched, sensor I2C ready");
    return ESP_OK;
}


i2c_master_bus_handle_t sensor_i2c_bus()
{
    return s_sensor_bus;
}


[[noreturn]] void power_off()
{
    ESP_LOGI(kTag, "Releasing power latch");
    // Wait for the button to be released, otherwise the latch re-arms.
    while (gpio_get_level(static_cast<gpio_num_t>(PIN_POWER_BTN)) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_hold_dis(static_cast<gpio_num_t>(PIN_POWER_HOLD));
    gpio_set_level(static_cast<gpio_num_t>(PIN_POWER_HOLD), 0);
    pulse_power_lock();
    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}

}  // namespace board
