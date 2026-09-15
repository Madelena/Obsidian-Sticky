// =============================================================================
// FONT
// =============================================================================
// The one translation unit that includes the generated font headers, so their
// static arrays exist exactly once.
#include "ui/font.h"

#include "ui/cjk_font.h"
#include "ui/fonts/font_body.h"
#include "ui/fonts/font_large.h"
#include "ui/fonts/font_small.h"
#include "ui/fonts/font_title.h"
#include "ui/text.h"

namespace font {

const Font &body()
{
    return font_body;
}


const Font &title()
{
    return font_title;
}


const Font &small()
{
    return font_small;
}


const Font &large()
{
    return font_large;
}


const FontGlyph &glyph(const Font &font, char character)
{
    unsigned char code = static_cast<unsigned char>(character);
    if (code < font.first || code > font.last) {
        code = '?';
    }
    return font.glyphs[code - font.first];
}


int text_width(const Font &font, const char *text)
{
    if (text == nullptr) {
        return 0;
    }
    int width = 0;
    size_t pos = 0;
    uint32_t code_point = 0;
    while ((code_point = text::next_code_point(text, pos)) != 0) {
        if (code_point >= 0x80 && cjk_font::has_glyph(code_point)) {
            width += cjk_font::advance(code_point, font);
        } else {
            width += glyph(font, static_cast<char>(code_point)).advance;
        }
    }
    return width;
}

}  // namespace font
