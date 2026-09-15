// =============================================================================
// BATTERY
// =============================================================================
// State of charge from the BQ27220 fuel gauge on the shared sensor bus, plus
// the USB-present pin. Read on demand by screen.cpp; nothing polls it.
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace battery {

// BATTERY INITIALIZER
// Attaches the fuel gauge to the sensor bus from board::sensor_i2c_bus() and
// configures the external-power input; returns ESP_ERR_NOT_FOUND if the gauge
// does not answer.
esp_err_t init(i2c_master_bus_handle_t bus);

// CHARGE READER
// Returns the state of charge in percent, or -1 when unavailable.
int percent();

// EXTERNAL POWER CHECKER
// Returns true while USB power is present.
bool on_usb();

}  // namespace battery
