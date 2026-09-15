// =============================================================================
// SCREEN
// =============================================================================
// Layout of the status band, optional caption, and paged note body on the
// 800x480 canvas. The note face comes from the note_size setting in
// settings.cpp.
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
constexpr int kCaptionTop = kStatusHeight + 12;
constexpr int kBlockGap = 6;
constexpr int kBodyTop = kStatusHeight + 18;
constexpr int kBodyBottom = canvas::kHeight - 24;
constexpr int kMeterWidth = 220;

std::string s_note;
std::string s_caption;
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
    if (size == "xlarge") {
        return font::xlarge();
    }
    return font::large();
}

// Gives the note body its top edge, pushed down when a caption is showing.
int body_top()
{
    return s_caption.empty() ? kBodyTop : kCaptionTop + font::small().line_height + kBlockGap;
}

// Draws the status band: text left, meter or page, Wi-Fi and battery right.
void draw_status(const std::string &status, int level)
{
    canvas::fill_rect(0, 0, canvas::kWidth, kStatusHeight, false);
    canvas::draw_text(font::title(), kMargin, 18, text::prepare(status).c_str());
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
    std::string line = right;
    if (s_pages > 1) {
        line = std::to_string(s_page + 1) + "/" + std::to_string(s_pages) + "   " + line;
    }
    canvas::draw_text_right(font::small(), canvas::kWidth - kMargin, 26, line.c_str());
}


// Draws the caption line, if there is one, just under the status separator.
void draw_caption()
{
    if (s_caption.empty()) {
        return;
    }
    canvas::draw_text(font::small(), kMargin, kCaptionTop, text::prepare(s_caption).c_str());
}


// Draws the current page of the note and records how many pages exist.
void draw_note()
{
    const Font &face = note_face();
    const int width = canvas::kWidth - 2 * kMargin;
    const std::vector<std::string> lines = text::wrap(face, text::prepare(s_note), width);
    const int pitch = face.line_height;
    const int top = body_top();
    const int per_page = (kBodyBottom - top) / pitch;
    s_pages = (static_cast<int>(lines.size()) + per_page - 1) / per_page;
    if (s_pages < 1) {
        s_pages = 1;
    }
    if (s_page >= s_pages) {
        s_page = s_pages - 1;
    }
    int y = top;
    const int first = s_page * per_page;
    for (int i = first; i < first + per_page && i < static_cast<int>(lines.size()); ++i) {
        canvas::draw_text(face, kMargin, y, lines[i].c_str());
        y += pitch;
    }
}


// Draws every band into the canvas.
void draw_all(int level)
{
    canvas::clear();
    draw_caption();
    draw_note();
    // Last, because draw_note is what counts the pages this band reports.
    draw_status(s_status, level);
}

}  // namespace


void set_note(const std::string &note)
{
    s_note = note;
    s_page = 0;
}


void set_caption(const std::string &text)
{
    s_caption = text;
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
    canvas::draw_text(font::title(), kMargin, 18, text::prepare(title).c_str());
    canvas::fill_rect(kMargin, kStatusHeight - 2, canvas::kWidth - 2 * kMargin, 2);

    // The only place the firmware version and LAN address are shown.
    const int stamp_top = kBodyBottom - font::small().height;
    int y = kBodyTop;
    const int width = canvas::kWidth - 2 * kMargin;
    for (const std::string &line : lines) {
        for (const std::string &wrapped : text::wrap(font::body(), text::prepare(line), width)) {
            if (y + font::body().height > stamp_top - kBlockGap) {
                break;
            }
            canvas::draw_text(font::body(), kMargin, y, wrapped.c_str());
            y += font::body().line_height;
        }
    }
    const std::string stamp =
        std::string("v") + esp_app_get_description()->version + "  " + wifi::ip();
    canvas::draw_text(font::small(), kMargin, stamp_top, stamp.c_str());
    display::refresh_full();
}

}  // namespace screen
