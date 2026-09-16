// =============================================================================
// BATTERY
// =============================================================================
// Cached BQ27220 state of charge and charge direction, plus the live
// external-power pin.
#include "board/battery.h"

#include <algorithm>

#include "bq27220.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "pin_config.h"

namespace battery {
namespace {

constexpr const char *kTag = "battery";
i2c_master_dev_handle_t s_device = nullptr;
int s_percent = -1;
bool s_charging = false;

}  // namespace


esp_err_t init(i2c_master_bus_handle_t bus)
{
    if (bus == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    gpio_config_t usb = {};
    usb.pin_bit_mask = 1ULL << PIN_EXTERNAL_POWER;
    usb.mode = GPIO_MODE_INPUT;
    gpio_config(&usb);

    if (s_device != nullptr) {
        return ESP_OK;
    }
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = BQ27220_I2C_ADDR;
    config.scl_speed_hz = 400000;
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &s_device);
    if (err != ESP_OK) {
        return err;
    }
    if (!bq27220_probe(s_device)) {
        ESP_LOGW(kTag, "BQ27220 not found at 0x%02X", BQ27220_I2C_ADDR);
        i2c_master_bus_rm_device(s_device);
        s_device = nullptr;
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}


void poll()
{
    uint16_t value = 0;
    s_percent = s_device != nullptr && bq27220_read_state_of_charge(s_device, value)
                    ? std::min<int>(value, 100)
                    : -1;

    // The charger's own status pin cannot tell "complete" from "disabled", so
    // the gauge answers instead: DSG is set while the pack is discharging, and
    // a clear bit with a cable attached is the only unambiguous charging this
    // board can report. A failed read says not charging rather than guessing.
    battery_status_t status = {};
    s_charging = s_device != nullptr && on_usb() &&
                 bq27220_read_battery_status(s_device, status.full) && !status.DSG;
}


int percent()
{
    return s_percent;
}


bool on_usb()
{
    return gpio_get_level(static_cast<gpio_num_t>(PIN_EXTERNAL_POWER)) == 1;
}


bool charging()
{
    return s_charging;
}

}  // namespace battery
