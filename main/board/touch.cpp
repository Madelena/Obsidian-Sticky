// =============================================================================
// TOUCH
// =============================================================================
// GT911 register access, power gating, and swipe detection. The register map
// and the reset timings are taken from Seeed's MIT-licensed gt911 component,
// credited in THIRD_PARTY.md.
#include "board/touch.h"

#include <atomic>
#include <cstdint>

#include "app/input.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"

namespace touch {
namespace {

constexpr const char *kTag = "touch";

// INT picks the address as RST rises: held low gives 0x5D, held high 0x14.
constexpr uint8_t kAddrIntLow = 0x5D;
constexpr uint8_t kAddrIntHigh = 0x14;
constexpr uint16_t kRegId = 0x8140;
constexpr uint16_t kRegResolution = 0x8146;
constexpr uint16_t kRegStatus = 0x814E;
constexpr uint16_t kRegPoints = 0x8150;
constexpr uint32_t kI2cHz = 100000;
constexpr int kI2cTimeoutMs = 20;

// Matches the wait in Seeed's own driver. The controller loads its
// configuration from internal flash while the rail comes up.
constexpr uint32_t kPowerOnMs = 250;
constexpr uint32_t kResetLowMs = 20;
constexpr uint32_t kResetHighMs = 20;
constexpr uint32_t kResetSettleMs = 80;
// Fast enough to sample a swipe, slow enough to leave the panel and the radio
// alone; a gesture lasts a couple of hundred milliseconds.
constexpr uint32_t kPollMs = 30;
constexpr uint32_t kSleepPollMs = 200;
// A lost release report would otherwise leave a gesture open forever.
constexpr uint32_t kReleaseTimeoutMs = 400;
// A swipe must cross an eighth of the screen and be more vertical than
// horizontal, so that picking the device up by the glass scrolls nothing.
constexpr int kTravelDivisor = 8;
constexpr int kMinTravelFloor = 40;

// The panel driver in ui/display.cpp hands the glass a canvas rotated 180
// degrees, so the controller's y grows towards the top of what is being read.
// Unverified: no unit has yet reported a coordinate. See docs/hardware.md.
constexpr bool kInvertY = true;

std::atomic<bool> s_enabled{false};
std::atomic<bool> s_failed{false};
TaskHandle_t s_task = nullptr;

i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_device = nullptr;
uint8_t s_address = kAddrIntHigh;   // Last address that answered, tried first
int s_max_y = 480;
int s_min_travel = 480 / kTravelDivisor;

bool s_tracking = false;
uint16_t s_start_x = 0;
uint16_t s_start_y = 0;
uint16_t s_last_x = 0;
uint16_t s_last_y = 0;
uint32_t s_last_report_ms = 0;

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// Reads len bytes from a 16-bit register address, big-endian on the wire.
bool read_registers(uint16_t reg, uint8_t *buffer, size_t len)
{
    if (s_device == nullptr) {
        return false;
    }
    const uint8_t address[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF)};
    return i2c_master_transmit_receive(s_device, address, sizeof(address), buffer, len,
                                       kI2cTimeoutMs) == ESP_OK;
}

// Writes one byte to a 16-bit register address.
bool write_register(uint16_t reg, uint8_t value)
{
    if (s_device == nullptr) {
        return false;
    }
    const uint8_t payload[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF),
                               value};
    return i2c_master_transmit(s_device, payload, sizeof(payload), kI2cTimeoutMs) == ESP_OK;
}

// Leaves a pin floating, so nothing trickles into the unpowered controller.
void float_pin(int pin)
{
    const auto gpio = static_cast<gpio_num_t>(pin);
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    gpio_pullup_dis(gpio);
    gpio_pulldown_dis(gpio);
}

// Resets the controller with INT held at the level that selects one address.
void reset_for_address(uint8_t address)
{
    const auto reset = static_cast<gpio_num_t>(PIN_TOUCH_RST);
    const auto interrupt = static_cast<gpio_num_t>(PIN_TOUCH_INT);
    gpio_set_direction(reset, GPIO_MODE_OUTPUT);
    gpio_set_direction(interrupt, GPIO_MODE_OUTPUT);
    gpio_set_level(reset, 0);
    gpio_set_level(interrupt, address == kAddrIntLow ? 0 : 1);
    vTaskDelay(pdMS_TO_TICKS(kResetLowMs));
    gpio_set_level(reset, 1);
    vTaskDelay(pdMS_TO_TICKS(kResetHighMs));
    // The level is only sampled across the rising edge; afterwards the pin is
    // the controller's own interrupt output again.
    gpio_set_direction(interrupt, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(kResetSettleMs));
}

// Puts the bus device for one address in place.
bool attach(uint8_t address)
{
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = kI2cHz;
    if (i2c_master_bus_add_device(s_bus, &config, &s_device) != ESP_OK) {
        s_device = nullptr;
        return false;
    }
    return true;
}

// Removes the bus device, if there is one.
void detach()
{
    if (s_device != nullptr) {
        i2c_master_bus_rm_device(s_device);
        s_device = nullptr;
    }
}

// Reads the sensor's own coordinate range and sizes the swipe threshold to it.
// A controller with no configuration loaded answers zero here and keeps the
// default, which is the state every unit tested so far has been in.
bool read_resolution()
{
    uint8_t raw[4] = {};
    if (!read_registers(kRegResolution, raw, sizeof(raw))) {
        return false;
    }
    const int max_y = raw[2] | (raw[3] << 8);
    if (max_y <= 0) {
        return false;
    }
    s_max_y = max_y;
    s_min_travel = s_max_y / kTravelDivisor;
    if (s_min_travel < kMinTravelFloor) {
        s_min_travel = kMinTravelFloor;
    }
    return true;
}

// Cuts power and releases every pin the controller shares.
void stop_controller()
{
    detach();
    if (s_bus != nullptr) {
        i2c_del_master_bus(s_bus);
        s_bus = nullptr;
    }
    float_pin(PIN_TOUCH_SDA);
    float_pin(PIN_TOUCH_SCL);
    float_pin(PIN_TOUCH_INT);
    gpio_set_direction(static_cast<gpio_num_t>(PIN_TOUCH_RST), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(PIN_TOUCH_RST), 0);
    gpio_set_direction(static_cast<gpio_num_t>(PIN_TOUCH_EN), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(PIN_TOUCH_EN), 0);
    s_tracking = false;
}

// Powers the controller, finds which address it answers on, and clears it
// down to a state where the next read is a fresh report.
bool start_controller()
{
    gpio_set_direction(static_cast<gpio_num_t>(PIN_TOUCH_EN), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(PIN_TOUCH_EN), 1);
    // Reset is released across the power-on, because board.cpp parks this pin
    // low and a rail that comes up with reset asserted never runs the
    // controller's own start-up.
    gpio_set_direction(static_cast<gpio_num_t>(PIN_TOUCH_RST), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(PIN_TOUCH_RST), 1);
    vTaskDelay(pdMS_TO_TICKS(kPowerOnMs));

    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = static_cast<gpio_num_t>(PIN_TOUCH_SDA);
    bus.scl_io_num = static_cast<gpio_num_t>(PIN_TOUCH_SCL);
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = 1;
    if (i2c_new_master_bus(&bus, &s_bus) != ESP_OK) {
        stop_controller();
        return false;
    }

    const uint8_t candidates[] = {
        s_address,
        s_address == kAddrIntLow ? kAddrIntHigh : kAddrIntLow,
    };
    for (uint8_t address : candidates) {
        reset_for_address(address);
        if (i2c_master_probe(s_bus, address, kI2cTimeoutMs) != ESP_OK || !attach(address)) {
            continue;
        }
        uint8_t id[4] = {};
        if (read_registers(kRegId, id, sizeof(id))) {
            s_address = address;
            const bool configured = read_resolution();
            write_register(kRegStatus, 0);
            s_tracking = false;
            // The resolution is the one cheap proof that a configuration was
            // loaded, and a controller without one never reports a touch.
            ESP_LOGI(kTag, "GT911 0x%02X id %.4s, %s", address,
                     reinterpret_cast<const char *>(id),
                     configured ? "configured" : "NO CONFIGURATION, touch will not report");
            return true;
        }
        detach();
    }
    stop_controller();
    return false;
}

// Ends a touch, posting a swipe when it travelled far enough to be one.
void finish_gesture()
{
    if (!s_tracking) {
        return;
    }
    s_tracking = false;
    const int dx = static_cast<int>(s_last_x) - static_cast<int>(s_start_x);
    const int raw_dy = static_cast<int>(s_last_y) - static_cast<int>(s_start_y);
    // Positive is down the screen as the reader sees it.
    const int dy = kInvertY ? -raw_dy : raw_dy;
    const int vertical = dy < 0 ? -dy : dy;
    const int horizontal = dx < 0 ? -dx : dx;
    if (vertical < s_min_travel || vertical <= horizontal) {
        ESP_LOGD(kTag, "gesture dropped, dx %d dy %d, needs %d", dx, dy, s_min_travel);
        return;
    }
    // Raw coordinates rather than the verdict alone, because a controller
    // mounted the other way up shows here before it shows as a dead gesture.
    ESP_LOGI(kTag, "swipe %s, raw (%u,%u) to (%u,%u)", dy < 0 ? "up" : "down",
             static_cast<unsigned>(s_start_x), static_cast<unsigned>(s_start_y),
             static_cast<unsigned>(s_last_x), static_cast<unsigned>(s_last_y));
    input::post(dy < 0 ? input::Event::SwipeUp : input::Event::SwipeDown);
}

// Reads one report and folds it into the gesture in progress.
void poll_once()
{
    uint8_t status = 0;
    if (!read_registers(kRegStatus, &status, 1)) {
        return;
    }
    if ((status & 0x80U) == 0U) {
        if (s_tracking && now_ms() - s_last_report_ms > kReleaseTimeoutMs) {
            finish_gesture();
        }
        return;
    }
    // A report with no points is the release, and the whole gesture is judged
    // on it, so the buffer is cleared before anything can return early.
    const uint8_t points = status & 0x0FU;
    uint8_t raw[8] = {};
    const bool touching = points > 0 && read_registers(kRegPoints, raw, sizeof(raw));
    write_register(kRegStatus, 0);
    if (!touching) {
        finish_gesture();
        return;
    }

    const uint16_t x = static_cast<uint16_t>(raw[0] | (raw[1] << 8));
    const uint16_t y = static_cast<uint16_t>(raw[2] | (raw[3] << 8));
    if (!s_tracking) {
        s_tracking = true;
        s_start_x = x;
        s_start_y = y;
    }
    s_last_x = x;
    s_last_y = y;
    s_last_report_ms = now_ms();
}

// Task body: owns the controller outright, so no lock guards the bus handles.
void run(void *)
{
    bool running = false;
    while (true) {
        const bool wanted = s_enabled.load();
        if (wanted && !running) {
            running = start_controller();
            if (!running) {
                // One failed bring-up is enough. Retrying would spend the
                // reset sequence again on every note that overflows.
                s_failed.store(true);
                s_enabled.store(false);
                ESP_LOGW(kTag, "GT911 did not answer, swipe scrolling is off");
            }
        } else if (!wanted && running) {
            stop_controller();
            running = false;
        }
        if (!running) {
            vTaskDelay(pdMS_TO_TICKS(kSleepPollMs));
            continue;
        }
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
}

}  // namespace


esp_err_t init()
{
    if (s_task != nullptr) {
        return ESP_OK;
    }
    return xTaskCreate(run, "touch", 3072, nullptr, 4, &s_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}


void set_enabled(bool enabled)
{
    if (enabled && s_failed.load()) {
        return;
    }
    s_enabled.store(enabled);
}

}  // namespace touch
