// =============================================================================
// CANVAS
// =============================================================================
// Pixel and text primitives on the packed 1-bit framebuffer.
#include "ui/canvas.h"

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "ui/cjk_font.h"
#include "ui/font.h"
#include "ui/text.h"

namespace canvas {
namespace {

uint8_t *s_buffer = nullptr;

// Sets one pixel; a set bit is white on this panel, so black clears it.
// The public set_pixel below forwards here so the fill and text loops keep
// inlining it at -Og.
inline void put_pixel(int x, int y, bool black)
{
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) {
        return;
    }
    uint8_t &byte = s_buffer[y * kStride + (x >> 3)];
    const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7));
    if (black) {
        byte &= static_cast<uint8_t>(~mask);
    } else {
        byte |= mask;
    }
}

}  // namespace


esp_err_t init()
{
    if (s_buffer != nullptr) {
        return ESP_OK;
    }
    s_buffer = static_cast<uint8_t *>(heap_caps_malloc(kSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_buffer == nullptr) {
        s_buffer = static_cast<uint8_t *>(heap_caps_malloc(kSize, MALLOC_CAP_8BIT));
    }
    if (s_buffer == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    clear();
    return ESP_OK;
}


void set_pixel(int x, int y, bool black)
{
    if (s_buffer != nullptr) {
        put_pixel(x, y, black);
    }
}


const uint8_t *data()
{
    return s_buffer;
}


void clear(bool white)
{
    if (s_buffer != nullptr) {
        std::memset(s_buffer, white ? 0xFF : 0x00, kSize);
    }
}


void fill_rect(int x, int y, int width, int height, bool black)
{
    if (s_buffer == nullptr) {
        return;
    }
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(kWidth, x + width);
    const int y1 = std::min(kHeight, y + height);
    for (int row = y0; row < y1; ++row) {
        for (int column = x0; column < x1; ++column) {
            put_pixel(column, row, black);
        }
    }
}


int draw_text(const Font &font, int x, int y, const char *text, bool black)
{
    if (s_buffer == nullptr || text == nullptr) {
        return x;
    }
    int pen = x;
    size_t pos = 0;
    uint32_t code_point = 0;
    while ((code_point = text::next_code_point(text, pos)) != 0) {
        if (code_point >= 0x80 && cjk_font::has_glyph(code_point)) {
            cjk_font::draw(code_point, pen, y, font, black);
            pen += cjk_font::advance(code_point, font);
            continue;
        }
        const FontGlyph &glyph = font::glyph(font, static_cast<char>(code_point));
        const int row_bytes = (glyph.width + 7) / 8;
        const uint8_t *rows = font.bitmap + glyph.offset;
        const int left = pen + glyph.x_offset;
        for (int row = 0; row < font.height; ++row) {
            const uint8_t *bits = rows + row * row_bytes;
            for (int column = 0; column < glyph.width; ++column) {
                if (bits[column >> 3] & (0x80U >> (column & 7))) {
                    put_pixel(left + column, y + row, black);
                }
            }
        }
        pen += glyph.advance;
    }
    return pen;
}


void draw_text_centered(const Font &font, int center_x, int y, const char *text, bool black)
{
    draw_text(font, center_x - font::text_width(font, text) / 2, y, text, black);
}


void draw_text_right(const Font &font, int right_x, int y, const char *text, bool black)
{
    draw_text(font, right_x - font::text_width(font, text), y, text, black);
}

}  // namespace canvas
