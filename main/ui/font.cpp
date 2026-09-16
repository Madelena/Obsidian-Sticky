// =============================================================================
// FONT
// =============================================================================
// The one translation unit that includes the generated font headers, so their
// static arrays exist exactly once, and the switch that picks a family.
#include "ui/font.h"

#include <cstring>

#include "ui/cjk_font.h"
#include "ui/fonts/atkinson_body.h"
#include "ui/fonts/atkinson_heading.h"
#include "ui/fonts/atkinson_large.h"
#include "ui/fonts/atkinson_small.h"
#include "ui/fonts/atkinson_title.h"
#include "ui/fonts/atkinson_xlarge.h"
#include "ui/fonts/atkinson_xxlarge.h"
#include "ui/fonts/inter_body.h"
#include "ui/fonts/inter_heading.h"
#include "ui/fonts/inter_large.h"
#include "ui/fonts/inter_small.h"
#include "ui/fonts/inter_title.h"
#include "ui/fonts/inter_xlarge.h"
#include "ui/fonts/inter_xxlarge.h"
#include "ui/fonts/literata_body.h"
#include "ui/fonts/literata_heading.h"
#include "ui/fonts/literata_large.h"
#include "ui/fonts/literata_small.h"
#include "ui/fonts/literata_title.h"
#include "ui/fonts/literata_xlarge.h"
#include "ui/fonts/literata_xxlarge.h"
#include "ui/fonts/opensans_body.h"
#include "ui/fonts/opensans_heading.h"
#include "ui/fonts/opensans_large.h"
#include "ui/fonts/opensans_small.h"
#include "ui/fonts/opensans_title.h"
#include "ui/fonts/opensans_xlarge.h"
#include "ui/fonts/opensans_xxlarge.h"
#include "ui/fonts/shantell_body.h"
#include "ui/fonts/shantell_heading.h"
#include "ui/fonts/shantell_large.h"
#include "ui/fonts/shantell_small.h"
#include "ui/fonts/shantell_title.h"
#include "ui/fonts/shantell_xlarge.h"
#include "ui/fonts/shantell_xxlarge.h"
#include "ui/text.h"

namespace font {
namespace {

// One family's seven faces, in the order the accessors below return them.
struct FaceSet {
    const Font *body;
    const Font *heading;
    const Font *title;
    const Font *small;
    const Font *large;
    const Font *xlarge;
    const Font *xxlarge;
};

// Indexed by Family, so the enum order and this table must stay in step.
constexpr FaceSet kFamilies[] = {
    {&inter_body, &inter_heading, &inter_title, &inter_small, &inter_large,
     &inter_xlarge, &inter_xxlarge},
    {&atkinson_body, &atkinson_heading, &atkinson_title, &atkinson_small, &atkinson_large,
     &atkinson_xlarge, &atkinson_xxlarge},
    {&opensans_body, &opensans_heading, &opensans_title, &opensans_small, &opensans_large,
     &opensans_xlarge, &opensans_xxlarge},
    {&literata_body, &literata_heading, &literata_title, &literata_small, &literata_large,
     &literata_xlarge, &literata_xxlarge},
    {&shantell_body, &shantell_heading, &shantell_title, &shantell_small, &shantell_large,
     &shantell_xlarge, &shantell_xxlarge},
};

const FaceSet *s_active = &kFamilies[static_cast<int>(Family::kInter)];

}  // namespace


void set_family(Family family)
{
    const int index = static_cast<int>(family);
    if (index < 0 || index >= static_cast<int>(sizeof(kFamilies) / sizeof(kFamilies[0]))) {
        return;
    }
    s_active = &kFamilies[index];
}


Family family_from_name(const char *name)
{
    if (name == nullptr) {
        return Family::kInter;
    }
    if (std::strcmp(name, "atkinson") == 0) {
        return Family::kAtkinson;
    }
    if (std::strcmp(name, "opensans") == 0) {
        return Family::kOpenSans;
    }
    if (std::strcmp(name, "literata") == 0) {
        return Family::kLiterata;
    }
    if (std::strcmp(name, "shantell") == 0) {
        return Family::kShantell;
    }
    return Family::kInter;
}


const Font &body()
{
    return *s_active->body;
}


const Font &heading()
{
    return *s_active->heading;
}


const Font &title()
{
    return *s_active->title;
}


const Font &small()
{
    return *s_active->small;
}


const Font &large()
{
    return *s_active->large;
}


const Font &xlarge()
{
    return *s_active->xlarge;
}


const Font &xxlarge()
{
    return *s_active->xxlarge;
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
