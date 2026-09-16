// =============================================================================
// TOUCH
// =============================================================================
// The GT911 capacitive panel, used for one thing: swiping a long note up and
// down. Swipes arrive in pipeline.cpp as input::Event values, exactly as
// button presses do.
#pragma once

#include "esp_err.h"

namespace touch {

// TOUCH INITIALIZER
// Starts the polling task and returns; the controller stays unpowered until
// set_enabled(true), so a device whose notes always fit never powers it.
esp_err_t init();

// TOUCH ENABLER
// Powers the controller and watches for swipes, or cuts its power and stops.
// Idempotent. The work runs on the touch task, so the roughly 150 ms of reset
// sequence is not spent on the caller's.
void set_enabled(bool enabled);

}  // namespace touch
