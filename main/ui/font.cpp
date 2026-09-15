// =============================================================================
// FONT
// =============================================================================
// The one translation unit that includes the generated font headers, so their
// static arrays exist exactly once.
#include "ui/font.h"

#include "ui/fonts/font_body.h"
#include "ui/fonts/font_small.h"
#include "ui/fonts/font_title.h"

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
    int width = 0;
    for (; text != nullptr && *text != '\0'; ++text) {
        width += glyph(font, *text).advance;
    }
    return width;
}

}  // namespace font
