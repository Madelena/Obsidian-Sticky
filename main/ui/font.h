// =============================================================================
// FONT
// =============================================================================
// Bitmap font structs shared by the generated headers in ui/fonts/ and the
// canvas text renderer. Layout must match what tools/gen_font.py emits.
#pragma once

#include <cstddef>
#include <cstdint>

struct FontGlyph {
    uint16_t offset;    // Byte offset into Font::bitmap
    uint8_t width;      // Bitmap width in pixels, rows are (width + 7) / 8 bytes
    uint8_t advance;    // Pen advance in pixels
    int8_t x_offset;    // Bitmap left edge relative to the pen position
};

struct Font {
    uint8_t height;       // Bitmap rows per glyph (ascent + descent)
    uint8_t baseline;     // Ascent in pixels, where cjk_font.cpp puts the baseline
    uint8_t line_height;  // Recommended line pitch
    uint8_t first;        // First encoded character (space)
    uint8_t last;         // Last encoded character (tilde)
    const FontGlyph *glyphs;
    const uint8_t *bitmap;
};

namespace font {

// FONT ACCESSORS
// Return the baked faces: 30 px regular, 30 px bold, 22 px regular, 40 px
// regular, 52 px regular, 64 px regular.
const Font &body();
const Font &title();
const Font &small();
const Font &large();
const Font &xlarge();
const Font &xxlarge();

// GLYPH LOOKUP
// Returns the glyph for a character, or the '?' glyph when it is not encoded.
const FontGlyph &glyph(const Font &font, char character);

// TEXT MEASURER
// Returns the advance width of a UTF-8 string in pixels, measuring anything
// outside ASCII through cjk_font when a font partition is present.
int text_width(const Font &font, const char *text);

}  // namespace font
