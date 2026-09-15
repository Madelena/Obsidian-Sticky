// =============================================================================
// INPUT
// =============================================================================
// Turns the three physical buttons into queued events. Callbacks only post;
// the pipeline task in pipeline.cpp drains the queue and does the work.
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

namespace input {

enum class Event : uint8_t {
    None,
    AiDown,     // AI button pressed: start recording immediately
    AiUp,       // AI button released: stop recording
    AiHeld,     // AI button held 5 s: power off
    UpClick,    // Up button: show the info screen
    DownClick,  // Down button: retry the failed stage
    DownHeld,   // Down button held 3 s: enter setup mode
};

// INPUT INITIALIZER
// Creates the debounced button devices and the event queue.
esp_err_t init();

// EVENT WAITER
// Blocks up to timeout for the next event; returns false on timeout.
bool wait(Event &event, TickType_t timeout);

// AI BUTTON PROBE
// Returns true while the AI button is physically held down.
bool ai_pressed();

}  // namespace input
