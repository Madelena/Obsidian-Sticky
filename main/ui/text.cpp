// =============================================================================
// TEXT
// =============================================================================
// UTF-8 decoding, ASCII folding, and greedy word wrapping for the mixed
// bitmap and TrueType text renderer in canvas.cpp.
#include "ui/text.h"

#include "ui/cjk_font.h"
#include "ui/font.h"

namespace text {
namespace {

// First code point treated as a standalone breakable unit: CJK radicals and
// everything above them, which includes kana, Hangul, and fullwidth forms.
constexpr uint32_t kWideFirst = 0x2E80;

// Base letters for U+00C0 through U+00FF, in code point order.
constexpr const char kLatin1Letters[] =
    "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";

// Appends the ASCII replacement for one code point.
void append_folded(std::string &out, uint32_t code_point)
{
    if (code_point == '\n' || code_point == '\r') {
        out += '\n';
    } else if (code_point == '\t') {
        out += ' ';
    } else if (code_point < 0x20) {
        return;
    } else if (code_point < 0x7F) {
        out += static_cast<char>(code_point);
    } else if (code_point == 0x00A0) {
        out += ' ';
    } else if (code_point == 0x00C6) {
        out += "AE";
    } else if (code_point == 0x00E6) {
        out += "ae";
    } else if (code_point == 0x00DF) {
        out += "ss";
    } else if (code_point >= 0x00C0 && code_point <= 0x00FF) {
        out += kLatin1Letters[code_point - 0x00C0];
    } else if (code_point == 0x2018 || code_point == 0x2019 || code_point == 0x201A) {
        out += '\'';
    } else if (code_point == 0x201C || code_point == 0x201D || code_point == 0x201E) {
        out += '"';
    } else if (code_point == 0x2013 || code_point == 0x2014 || code_point == 0x2212) {
        out += '-';
    } else if (code_point == 0x2022 || code_point == 0x00B7) {
        out += '-';
    } else if (code_point == 0x2026) {
        out += "...";
    } else {
        out += '?';
    }
}

// Measures one code point's byte run, which keeps every width in wrap() on
// the same path as canvas::draw_text.
int run_width(const Font &font, const std::string &source, size_t begin, size_t end)
{
    return font::text_width(font, source.substr(begin, end - begin).c_str());
}

}  // namespace


uint32_t next_code_point(const std::string &utf8, size_t &pos)
{
    return next_code_point(utf8.c_str(), pos);
}


uint32_t next_code_point(const char *utf8, size_t &pos)
{
    const unsigned char lead = static_cast<unsigned char>(utf8[pos]);
    if (lead == 0) {
        return 0;
    }
    ++pos;
    if (lead < 0x80) {
        return lead;
    }

    uint32_t code_point = 0;
    size_t extra = 0;
    if (lead >= 0xF0 && lead <= 0xF7) {
        code_point = lead & 0x07;
        extra = 3;
    } else if (lead >= 0xE0) {
        code_point = lead & 0x0F;
        extra = 2;
    } else if (lead >= 0xC2) {
        code_point = lead & 0x1F;
        extra = 1;
    } else {
        // A stray continuation byte or an overlong two-byte lead.
        return 0xFFFD;
    }

    for (size_t k = 0; k < extra; ++k) {
        const unsigned char next = static_cast<unsigned char>(utf8[pos]);
        if ((next & 0xC0) != 0x80) {
            return 0xFFFD;
        }
        code_point = (code_point << 6) | (next & 0x3F);
        ++pos;
    }
    if (code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
        return 0xFFFD;
    }
    return code_point;
}


std::string prepare(const std::string &utf8)
{
    std::string out;
    out.reserve(utf8.size());
    const bool font_ready = cjk_font::available();
    size_t pos = 0;
    while (pos < utf8.size()) {
        const size_t start = pos;
        const uint32_t code_point = next_code_point(utf8, pos);
        if (code_point >= 0x80 && font_ready && cjk_font::has_glyph(code_point)) {
            out.append(utf8, start, pos - start);
        } else {
            append_folded(out, code_point);
        }
    }
    return out;
}


std::vector<std::string> wrap(const Font &font, const std::string &prepared, int max_width)
{
    std::vector<std::string> lines;
    std::string line;
    int line_width = 0;
    const int space_width = font::glyph(font, ' ').advance;

    // Flushes the current line, even when empty, so blank lines survive.
    auto flush = [&]() {
        lines.push_back(line);
        line.clear();
        line_width = 0;
    };

    size_t pos = 0;
    while (pos < prepared.size()) {
        const size_t start = pos;
        const uint32_t code_point = next_code_point(prepared, pos);
        if (code_point == '\n') {
            flush();
            continue;
        }
        if (code_point == ' ') {
            if (!line.empty()) {
                line += ' ';
                line_width += space_width;
            }
            continue;
        }

        // A CJK code point breaks on both sides, so it is its own word.
        if (code_point >= kWideFirst) {
            const int glyph_width = run_width(font, prepared, start, pos);
            if (line_width + glyph_width > max_width && !line.empty()) {
                flush();
            }
            line.append(prepared, start, pos - start);
            line_width += glyph_width;
            continue;
        }

        // A Latin word runs until a space, a newline, or a CJK code point.
        size_t end = pos;
        while (end < prepared.size()) {
            size_t probe = end;
            const uint32_t next = next_code_point(prepared, probe);
            if (next == ' ' || next == '\n' || next >= kWideFirst) {
                break;
            }
            end = probe;
        }
        std::string word = prepared.substr(start, end - start);
        pos = end;
        int word_width = font::text_width(font, word.c_str());

        if (line_width + word_width > max_width && !line.empty()) {
            flush();
        }
        // A word wider than the line is split between code points.
        while (word_width > max_width) {
            size_t fit = 0;
            int fit_width = 0;
            size_t probe = 0;
            while (probe < word.size()) {
                size_t after = probe;
                next_code_point(word, after);
                const int advance = run_width(font, word, probe, after);
                if (fit_width + advance > max_width) {
                    break;
                }
                fit_width += advance;
                fit = after;
                probe = after;
            }
            if (fit == 0) {
                next_code_point(word, fit);
            }
            line = word.substr(0, fit);
            flush();
            word.erase(0, fit);
            word_width = font::text_width(font, word.c_str());
        }
        line += word;
        line_width += word_width;
    }
    // Drop a trailing space so right-aligned measurements stay honest.
    if (!line.empty() && line.back() == ' ') {
        line.pop_back();
    }
    if (!line.empty() || lines.empty()) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace text
