// =============================================================================
// BATTERY
// =============================================================================
// BQ27220 state of charge and the external-power pin.
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


int percent()
{
    uint16_t value = 0;
    if (s_device == nullptr || !bq27220_read_state_of_charge(s_device, value)) {
        return -1;
    }
    return std::min<int>(value, 100);
}


bool on_usb()
{
    return gpio_get_level(static_cast<gpio_num_t>(PIN_EXTERNAL_POWER)) == 1;
}

}  // namespace battery
