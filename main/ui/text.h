// =============================================================================
// TEXT
// =============================================================================
// String preparation for the canvas: UTF-8 decoding, folding of anything the
// fonts cannot draw, and word wrapping. Used by screen.cpp before anything
// reaches the canvas.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Font;

namespace text {

// UTF-8 DECODER
// Decodes the sequence at pos, advances pos past it, and returns the code
// point, or 0xFFFD when the bytes are malformed and 0 at the end of a
// NUL-terminated string.
uint32_t next_code_point(const std::string &utf8, size_t &pos);
uint32_t next_code_point(const char *utf8, size_t &pos);

// TEXT PREPARER
// Returns a drawable string: code points the fonts can render are kept as
// UTF-8, and everything else folds to an ASCII approximation such as '-' for
// an en dash or '?' for an unknown glyph.
std::string prepare(const std::string &utf8);

// WORD WRAPPER
// Splits prepared text into lines no wider than max_width pixels, breaking at
// spaces, newlines, between CJK code points, or inside a Latin word only when
// it cannot fit on a line.
std::vector<std::string> wrap(const Font &font, const std::string &prepared, int max_width);

}  // namespace text
