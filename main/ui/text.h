// =============================================================================
// TEXT
// =============================================================================
// UTF-8 decoding, truncation, folding of anything the fonts cannot draw, and
// word wrapping. screen.cpp runs prepare() then wrap() before anything reaches
// the canvas; anything with a byte budget uses truncate_utf8().
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

// UTF-8 TRUNCATOR
// Returns the first max bytes of utf8, backed off to a code point boundary so
// the result never ends in half a character. Callers with a byte budget want
// this rather than substr: a cut through a multi-byte sequence reloads as a
// stray U+FFFD from next_code_point().
std::string truncate_utf8(const std::string &utf8, size_t max);

// WORD WRAPPER
// Splits prepared text into lines no wider than max_width pixels, breaking at
// spaces, newlines, between CJK code points, or inside a Latin word only when
// it cannot fit on a line.
std::vector<std::string> wrap(const Font &font, const std::string &prepared, int max_width);

}  // namespace text
