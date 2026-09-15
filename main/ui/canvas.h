// =============================================================================
// CANVAS
// =============================================================================
// The 800x480 one-bit framebuffer in logical landscape coordinates with the
// origin top-left. display.cpp rotates it for the panel; nothing else knows
// about the physical orientation.
#pragma once

#include <cstdint>

#include "esp_err.h"

struct Font;

namespace canvas {

constexpr int kWidth = 800;
constexpr int kHeight = 480;
constexpr int kStride = kWidth / 8;
constexpr int kSize = kStride * kHeight;

// CANVAS INITIALIZER
// Allocates the framebuffer in PSRAM and clears it to white.
esp_err_t init();

// FRAMEBUFFER GETTER
// Returns the packed 1-bit buffer (1 = white, MSB first, 100-byte rows).
const uint8_t *data();

// PIXEL SETTER
// Sets one pixel black or white, ignoring coordinates off the canvas.
void set_pixel(int x, int y, bool black);

// CANVAS CLEARER
// Fills the whole buffer with white, or black when white is false.
void clear(bool white = true);

// RECT FILLER
// Fills an axis-aligned rectangle, clipping to the canvas.
void fill_rect(int x, int y, int width, int height, bool black = true);

// TEXT DRAWER
// Draws one line of UTF-8 text with its top-left at (x, y) using only black
// pixels, leaving the background as is; returns the pen x after the last
// glyph.
int draw_text(const Font &font, int x, int y, const char *text, bool black = true);

// CENTERED TEXT DRAWER
// Draws one line centered horizontally on center_x.
void draw_text_centered(const Font &font, int center_x, int y, const char *text, bool black = true);

// RIGHT-ALIGNED TEXT DRAWER
// Draws one line ending at right_x.
void draw_text_right(const Font &font, int right_x, int y, const char *text, bool black = true);

}  // namespace canvas
