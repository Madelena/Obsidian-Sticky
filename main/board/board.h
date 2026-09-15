// =============================================================================
// BOARD
// =============================================================================
// Power latch, shared buses, and the pins every other module assumes are in a
// safe state. Called first from main.cpp; drivers receive bus handles from here
// rather than creating their own.
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace board {

// BOARD INITIALIZER
// Latches board power, parks unused peripherals, and creates the sensor I2C
// bus. Returns the first failing esp_err_t.
esp_err_t init();

// SENSOR BUS GETTER
// Returns the shared I2C1 handle for the fuel gauge and RTC, null before init.
i2c_master_bus_handle_t sensor_i2c_bus();

// BOARD POWER-OFF
// Releases the power latch so the board turns off; does not return.
[[noreturn]] void power_off();

// WAKE CHECKER
// Returns true when this boot was a deep-sleep wake from the AI button.
bool woke_from_button();

}  // namespace board
