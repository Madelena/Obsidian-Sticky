// =============================================================================
// CJK FONT
// =============================================================================
// Draw-time TrueType rendering for every code point the ASCII bitmap faces in
// font.h cannot encode, read straight out of the memory-mapped `font` flash
// partition.
#pragma once

#include <cstdint>

#include "esp_err.h"

struct Font;

namespace cjk_font {

// CJK FONT INITIALIZER
// Maps the `font` partition and parses the TrueType header, returning
// ESP_ERR_NOT_FOUND when the partition is missing or holds no font.
esp_err_t init();

// AVAILABILITY REPORTER
// Reports whether a usable font was parsed, so callers can fall back to the
// ASCII folding in text.cpp.
bool available();

// GLYPH PRESENCE CHECKER
// Reports whether the font has a real glyph for a code point.
bool has_glyph(uint32_t code_point);

// ADVANCE MEASURER
// Returns the pen advance in pixels for a code point rendered to match the
// bitmap face `face`.
int advance(uint32_t code_point, const Font &face);

// GLYPH DRAWER
// Rasterizes one code point into the canvas with its top-left cell at (x, y),
// sharing the baseline with bitmap text drawn at the same y.
void draw(uint32_t code_point, int x, int y, const Font &face, bool black);

}  // namespace cjk_font
