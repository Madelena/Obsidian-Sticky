// =============================================================================
// TEXT
// =============================================================================
// UTF-8 folding and greedy word wrapping for the ASCII bitmap fonts.
#include "ui/text.h"

#include <cstdint>

#include "ui/font.h"

namespace text {
namespace {

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

}  // namespace


std::string fold_to_ascii(const std::string &utf8)
{
    std::string out;
    out.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char lead = static_cast<unsigned char>(utf8[i]);
        uint32_t code_point = lead;
        size_t extra = 0;
        if (lead >= 0xF0) {
            code_point = lead & 0x07;
            extra = 3;
        } else if (lead >= 0xE0) {
            code_point = lead & 0x0F;
            extra = 2;
        } else if (lead >= 0xC0) {
            code_point = lead & 0x1F;
            extra = 1;
        }
        // A truncated sequence is folded as '?' rather than read past the end.
        if (i + extra >= utf8.size() + (extra == 0 ? 1 : 0)) {
            if (extra != 0) {
                out += '?';
                break;
            }
        }
        for (size_t k = 1; k <= extra; ++k) {
            code_point = (code_point << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        }
        append_folded(out, code_point);
        i += extra + 1;
    }
    return out;
}


std::vector<std::string> wrap(const Font &font, const std::string &ascii, int max_width)
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
    while (pos < ascii.size()) {
        if (ascii[pos] == '\n') {
            flush();
            ++pos;
            continue;
        }
        if (ascii[pos] == ' ') {
            if (!line.empty()) {
                line += ' ';
                line_width += space_width;
            }
            ++pos;
            continue;
        }

        size_t end = ascii.find_first_of(" \n", pos);
        if (end == std::string::npos) {
            end = ascii.size();
        }
        std::string word = ascii.substr(pos, end - pos);
        int word_width = font::text_width(font, word.c_str());

        if (line_width + word_width > max_width && !line.empty()) {
            flush();
        }
        // A word wider than the line is split by characters.
        while (word_width > max_width) {
            size_t fit = 0;
            int fit_width = 0;
            while (fit < word.size()) {
                const int advance = font::glyph(font, word[fit]).advance;
                if (fit_width + advance > max_width) {
                    break;
                }
                fit_width += advance;
                ++fit;
            }
            if (fit == 0) {
                fit = 1;
            }
            line = word.substr(0, fit);
            flush();
            word.erase(0, fit);
            word_width = font::text_width(font, word.c_str());
        }
        line += word;
        line_width += word_width;
        pos = end;
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
