// =============================================================================
// SCREEN
// =============================================================================
// Layout of the status band, paged note body, and footer on the 800x480
// canvas. The note face comes from the note_size setting in settings.cpp.
#include "ui/screen.h"

#include <cstdio>

#include "app/settings.h"
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
std::string s_status;
int s_page = 0;
int s_pages = 1;

// Picks the note face from the user's size setting.
const Font &note_face()
{
    const std::string size = settings::get().note_size;
    if (size == "small") {
        return font::small();
    }
    if (size == "medium") {
        return font::body();
    }
    return font::large();
}

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

// Draws the current page of the note and records how many pages exist.
void draw_note()
{
    const Font &face = note_face();
    const int width = canvas::kWidth - 2 * kMargin;
    const std::vector<std::string> lines = text::wrap(face, text::fold_to_ascii(s_note), width);
    const int pitch = face.line_height;
    const int per_page = (kBodyBottom - kBodyTop) / pitch;
    s_pages = (static_cast<int>(lines.size()) + per_page - 1) / per_page;
    if (s_pages < 1) {
        s_pages = 1;
    }
    if (s_page >= s_pages) {
        s_page = s_pages - 1;
    }
    int y = kBodyTop;
    const int first = s_page * per_page;
    for (int i = first; i < first + per_page && i < static_cast<int>(lines.size()); ++i) {
        canvas::draw_text(face, kMargin, y, lines[i].c_str());
        y += pitch;
    }
}

// Draws the footer: message left, page count, version and address right.
void draw_footer()
{
    canvas::fill_rect(kMargin, kFooterTop, canvas::kWidth - 2 * kMargin, 1);
    canvas::draw_text(font::small(), kMargin, kFooterTop + 16, text::fold_to_ascii(s_footer).c_str());
    std::string right = std::string("v") + esp_app_get_description()->version + "  " + wifi::ip();
    if (s_pages > 1) {
        right = "Page " + std::to_string(s_page + 1) + "/" + std::to_string(s_pages) + "   " + right;
    }
    canvas::draw_text_right(font::small(), canvas::kWidth - kMargin, kFooterTop + 16, right.c_str());
}

// Draws every band into the canvas.
void draw_all(int level)
{
    canvas::clear();
    draw_note();
    draw_status(s_status, level);
    draw_footer();
}

}  // namespace


void set_note(const std::string &note)
{
    s_note = note;
    s_page = 0;
}


void set_footer(const std::string &footer)
{
    s_footer = footer;
}


void show(const std::string &status, int level, bool full)
{
    s_status = status;
    draw_all(level);
    if (full) {
        display::refresh_full();
    } else {
        display::refresh_partial();
    }
}


bool scroll(int delta)
{
    const int target = s_page + delta;
    if (target < 0 || target >= s_pages) {
        return false;
    }
    s_page = target;
    draw_all(-1);
    display::refresh_partial();
    return true;
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
