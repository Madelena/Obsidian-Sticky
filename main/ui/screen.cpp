// =============================================================================
// SCREEN
// =============================================================================
// Layout of the status band, note body, and footer on the 800x480 canvas.
#include "ui/screen.h"

#include <cstdio>

#include "board/battery.h"
#include "esp_app_desc.h"
#include "net/wifi.h"
#include "ui/canvas.h"
#include "ui/display.h"
#include "ui/font.h"
#include "ui/text.h"

namespace screen {
namespace {

constexpr int kMargin = 36;
constexpr int kStatusHeight = 76;
constexpr int kFooterTop = 416;
constexpr int kBodyTop = kStatusHeight + 18;
constexpr int kBodyBottom = kFooterTop - 12;
constexpr int kMeterWidth = 220;

std::string s_note;
std::string s_footer;

// Draws the status band: text left, meter or Wi-Fi and battery right.
void draw_status(const std::string &status, int level)
{
    canvas::fill_rect(0, 0, canvas::kWidth, kStatusHeight, false);
    canvas::draw_text(font::title(), kMargin, 18, text::fold_to_ascii(status).c_str());
    canvas::fill_rect(kMargin, kStatusHeight - 2, canvas::kWidth - 2 * kMargin, 2);

    if (level >= 0) {
        const int x = canvas::kWidth - kMargin - kMeterWidth;
        canvas::fill_rect(x, 28, kMeterWidth, 22, true);
        canvas::fill_rect(x + 2, 30, kMeterWidth - 4, 18, false);
        canvas::fill_rect(x + 2, 30, (kMeterWidth - 4) * level / 100, 18, true);
        return;
    }

    char right[48];
    const int percent = battery::percent();
    std::snprintf(right, sizeof(right), "%s   %s%s", wifi::connected() ? "Wi-Fi" : "No Wi-Fi",
                  percent >= 0 ? (std::to_string(percent) + "%").c_str() : "--",
                  battery::on_usb() ? " USB" : "");
    canvas::draw_text_right(font::small(), canvas::kWidth - kMargin, 26, right);
}

// Draws the note, choosing the largest font that fits and truncating with an
// ellipsis line when even the small font overflows.
void draw_note()
{
    const int width = canvas::kWidth - 2 * kMargin;
    const std::string ascii = text::fold_to_ascii(s_note);
    const Font *face = &font::body();
    std::vector<std::string> lines = text::wrap(*face, ascii, width);
    int pitch = face->line_height;
    int available = (kBodyBottom - kBodyTop) / pitch;
    if (static_cast<int>(lines.size()) > available) {
        face = &font::small();
        lines = text::wrap(*face, ascii, width);
        pitch = face->line_height;
        available = (kBodyBottom - kBodyTop) / pitch;
    }
    if (static_cast<int>(lines.size()) > available) {
        lines.resize(available);
        lines.back() = lines.back().substr(0, lines.back().size() > 3 ? lines.back().size() - 3 : 0) + "...";
    }
    int y = kBodyTop;
    for (const std::string &line : lines) {
        canvas::draw_text(*face, kMargin, y, line.c_str());
        y += pitch;
    }
}

// Draws the footer: message left, version and address right.
void draw_footer()
{
    canvas::fill_rect(kMargin, kFooterTop, canvas::kWidth - 2 * kMargin, 1);
    canvas::draw_text(font::small(), kMargin, kFooterTop + 16, text::fold_to_ascii(s_footer).c_str());
    const std::string right = std::string("v") + esp_app_get_description()->version + "  " + wifi::ip();
    canvas::draw_text_right(font::small(), canvas::kWidth - kMargin, kFooterTop + 16, right.c_str());
}

}  // namespace


void set_note(const std::string &note)
{
    s_note = note;
}


void set_footer(const std::string &footer)
{
    s_footer = footer;
}


void show(const std::string &status, int level, bool full)
{
    canvas::clear();
    draw_status(status, level);
    draw_note();
    draw_footer();
    if (full) {
        display::refresh_full();
    } else {
        display::refresh_partial();
    }
}


void show_message(const std::string &title, const std::vector<std::string> &lines)
{
    canvas::clear();
    canvas::draw_text(font::title(), kMargin, 18, text::fold_to_ascii(title).c_str());
    canvas::fill_rect(kMargin, kStatusHeight - 2, canvas::kWidth - 2 * kMargin, 2);
    int y = kBodyTop;
    const int width = canvas::kWidth - 2 * kMargin;
    for (const std::string &line : lines) {
        for (const std::string &wrapped : text::wrap(font::body(), text::fold_to_ascii(line), width)) {
            if (y > kBodyBottom - font::body().height) {
                break;
            }
            canvas::draw_text(font::body(), kMargin, y, wrapped.c_str());
            y += font::body().line_height;
        }
    }
    draw_footer();
    display::refresh_full();
}

}  // namespace screen
