// =============================================================================
// DISPLAY
// =============================================================================
// Owns the SSD1677 panel and pushes the canvas to it. The only module that
// knows the panel is mounted upside down relative to the logical canvas.
#pragma once

#include "esp_err.h"

namespace display {

// DISPLAY INITIALIZER
// Powers the panel, sets up SPI2, and allocates the rotation buffer.
esp_err_t init();

// FULL REFRESHER
// Rotates the canvas now and hands the full monochrome waveform to the render
// task, returning before it completes so the caller can get on with the
// network. The waveform is slow, flashes, and leaves a clean image.
esp_err_t refresh_full();

// PARTIAL REFRESHER
// Sends the canvas with the fast partial waveform; every 20th call is
// promoted to a full refresh to clear accumulated ghosting.
esp_err_t refresh_partial();

// REFRESH WAITER
// Blocks until a refresh handed over earlier has finished its waveform. Only
// needed before something that must see a settled panel; ordinary drawing
// already waits for its predecessor.
void wait_idle();

// DISPLAY SLEEPER
// Puts the controller to sleep and cuts panel power; the image stays visible.
esp_err_t sleep();

}  // namespace display
