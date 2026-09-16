// =============================================================================
// BATTERY
// =============================================================================
// State of charge from the BQ27220 fuel gauge on the shared sensor bus, plus
// the USB-present pin. pipeline.cpp polls the gauge; every other reader takes
// the cached values, so no draw waits on the sensor bus.
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace battery {

// BATTERY INITIALIZER
// Attaches the fuel gauge to the sensor bus from board::sensor_i2c_bus() and
// configures the external-power input; returns ESP_ERR_NOT_FOUND if the gauge
// does not answer.
esp_err_t init(i2c_master_bus_handle_t bus);

// GAUGE POLLER
// Refreshes the cached state of charge and charge direction over I2C. Only
// the pipeline task may call it: this is the one place the firmware touches
// the sensor bus for the battery. See pipeline.cpp run(), idle branch.
void poll();

// CHARGE READER
// Returns the state of charge in percent as of the last poll(), or -1 when
// the gauge did not answer.
int percent();

// EXTERNAL POWER CHECKER
// Returns true while USB power is present, read live from the pin so a cable
// shows up without waiting for a poll.
bool on_usb();

// CHARGE DIRECTION CHECKER
// Returns true when the last poll() found current flowing into the pack,
// which is what separates charging from merely plugged in.
bool charging();

}  // namespace battery
