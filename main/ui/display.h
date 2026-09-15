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
// Sends the canvas with the full monochrome waveform (slow, flashes, clean).
esp_err_t refresh_full();

// PARTIAL REFRESHER
// Sends the canvas with the fast partial waveform; every 20th call is
// promoted to a full refresh to clear accumulated ghosting.
esp_err_t refresh_partial();

// DISPLAY SLEEPER
// Puts the controller to sleep and cuts panel power; the image stays visible.
esp_err_t sleep();

}  // namespace display
