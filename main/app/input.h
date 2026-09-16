// =============================================================================
// INPUT
// =============================================================================
// Turns the three physical buttons into queued events. Callbacks only post;
// the pipeline task in pipeline.cpp drains the queue and does the work. The
// touch panel in board/touch.cpp posts its swipes into the same queue.
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

namespace input {

enum class Event : uint8_t {
    None,
    AiDown,     // AI button pressed: start recording immediately
    AiUp,       // AI button released: stop recording
    UpClick,    // Up button: scroll back, or show the info screen
    UpHeld,     // Up button held 3 s: power off
    DownClick,  // Down button: scroll on, or retry the failed stage
    DownHeld,   // Down button held 3 s: enter setup mode
    SwipeUp,    // Finger swiped up the panel: show the next screen of the note
    SwipeDown,  // Finger swiped down the panel: show the previous screen
};

// INPUT INITIALIZER
// Creates the debounced button devices and the event queue.
esp_err_t init();

// EVENT WAITER
// Blocks up to timeout for the next event; returns false on timeout.
bool wait(Event &event, TickType_t timeout);

// EVENT POSTER
// Queues an event from a source that is not one of the buttons. Never blocks,
// so an event is dropped rather than stalling the caller's task.
void post(Event event);

// AI BUTTON PROBE
// Returns true while the AI button is physically held down.
bool ai_pressed();

}  // namespace input
