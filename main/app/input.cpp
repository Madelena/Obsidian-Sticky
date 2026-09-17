// =============================================================================
// INPUT
// =============================================================================
// espressif/button wiring for the AI, Up, and Down buttons.
#include "app/input.h"

#include "button_gpio.h"
#include "esp_check.h"
#include "freertos/queue.h"
#include "iot_button.h"
#include "pin_config.h"

namespace input {
namespace {

constexpr const char *kTag = "input";
constexpr uint16_t kShortPressMs = 180;
// Up alone gets a wider window, because this one constant is both the delay
// before a single click fires and the gap allowed between the two presses of
// a double click. At 180 ms most double taps miss, and a missed one opens the
// info screen and then dismisses it: two full refreshes to end up where you
// started. The cost is 120 ms more before an Up scroll, against a partial
// refresh of 880 ms. See PRESS_REPEAT_DOWN_CHECK in components/button.
constexpr uint16_t kUpShortPressMs = 250;
// The AI button cannot carry a long press: holding it is how you record, and
// record_loop() in pipeline.cpp reads the raw level rather than the queue so a
// release lands even mid-refresh. Power off lives on Up instead, matching
// setup mode on Down.
constexpr uint16_t kHoldMs = 3000;

QueueHandle_t s_queue = nullptr;
button_handle_t s_ai = nullptr;
button_handle_t s_up = nullptr;
button_handle_t s_down = nullptr;

// Posts the event encoded in user_data; runs in the button timer context.
void post_event(void *, void *user_data)
{
    const Event event = static_cast<Event>(reinterpret_cast<uintptr_t>(user_data));
    if (s_queue != nullptr) {
        xQueueSend(s_queue, &event, 0);
    }
}

// Registers one button event with the Event it should post.
esp_err_t on(button_handle_t handle, button_event_t button_event, Event event)
{
    return iot_button_register_cb(handle, button_event, nullptr, post_event,
                                  reinterpret_cast<void *>(static_cast<uintptr_t>(event)));
}

// Creates an active-low GPIO button with its own press thresholds.
esp_err_t create(int gpio, uint16_t long_press_ms, uint16_t short_press_ms,
                 button_handle_t *handle)
{
    button_config_t config = {};
    config.long_press_time = long_press_ms;
    config.short_press_time = short_press_ms;
    button_gpio_config_t pin = {};
    pin.gpio_num = gpio;
    pin.active_level = 0;
    pin.enable_power_save = false;
    pin.disable_pull = false;
    return iot_button_new_gpio_device(&config, &pin, handle);
}

}  // namespace


esp_err_t init()
{
    if (s_queue != nullptr) {
        return ESP_OK;
    }
    s_queue = xQueueCreate(8, sizeof(Event));
    if (s_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(create(PIN_POWER_BTN, kHoldMs, kShortPressMs, &s_ai), kTag, "ai button");
    ESP_RETURN_ON_ERROR(on(s_ai, BUTTON_PRESS_DOWN, Event::AiDown), kTag, "ai down");
    ESP_RETURN_ON_ERROR(on(s_ai, BUTTON_PRESS_UP, Event::AiUp), kTag, "ai up");

    ESP_RETURN_ON_ERROR(create(PIN_BTN_UP, kHoldMs, kUpShortPressMs, &s_up), kTag, "up button");
    ESP_RETURN_ON_ERROR(on(s_up, BUTTON_SINGLE_CLICK, Event::UpClick), kTag, "up click");
    // Costs the single click nothing: it already waits out the same window
    // before firing, whether or not anything is listening for a double.
    ESP_RETURN_ON_ERROR(on(s_up, BUTTON_DOUBLE_CLICK, Event::UpDouble), kTag, "up double");
    ESP_RETURN_ON_ERROR(on(s_up, BUTTON_LONG_PRESS_START, Event::UpHeld), kTag, "up held");

    ESP_RETURN_ON_ERROR(create(PIN_BTN_DOWN, kHoldMs, kShortPressMs, &s_down), kTag, "down button");
    ESP_RETURN_ON_ERROR(on(s_down, BUTTON_SINGLE_CLICK, Event::DownClick), kTag, "down click");
    ESP_RETURN_ON_ERROR(on(s_down, BUTTON_LONG_PRESS_START, Event::DownHeld), kTag, "down held");
    return ESP_OK;
}


bool wait(Event &event, TickType_t timeout)
{
    return s_queue != nullptr && xQueueReceive(s_queue, &event, timeout) == pdTRUE;
}


void post(Event event)
{
    if (s_queue != nullptr) {
        xQueueSend(s_queue, &event, 0);
    }
}


bool ai_pressed()
{
    return s_ai != nullptr && iot_button_get_key_level(s_ai) == 1;
}

}  // namespace input
