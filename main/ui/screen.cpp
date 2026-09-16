// =============================================================================
// SCREEN
// =============================================================================
// Layout of the status band, optional caption, and scrolling note body on the
// 800x480 canvas. The note face comes from the text_size setting in
// settings.cpp, which may ask for the largest face that shows the note whole.
#include "ui/screen.h"

#include <cstdio>
#include <mutex>

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
// Twelve above and below is what fits 9, 7, 5 and 4 lines of the four faces
// while leaving the lowest ink, a full medium page, 12 px clear of the edge.
constexpr int kBodyTop = kStatusHeight + 12;
constexpr int kBodyBottom = canvas::kHeight - 12;
constexpr int kMeterWidth = 220;
// The scroll bar sits inside the right margin rather than taking a column of
// its own, so the text width does not depend on whether the note overflows.
constexpr int kScrollGap = 12;
constexpr int kScrollWidth = 8;
constexpr int kScrollThumbMin = 28;

// The pipeline task and the portal's HTTP task both draw here: the portal
// redraws on a settings save and on POST /api/show. The canvas is one shared
// buffer, so without this the two could interleave into a torn frame.
// display.cpp takes its own lock underneath, always in this order.
std::mutex s_mutex;

std::string s_note;
std::string s_caption;
std::string s_status;
int s_first_line = 0;    // Topmost wrapped line on screen
int s_total_lines = 0;
int s_visible_lines = 1;
bool s_radio_off = false;

// One face and the note wrapped to it, as chosen for the space available.
struct Layout {
    const Font *face = nullptr;
    std::vector<std::string> lines;
    int visible = 1;
};

// Picks the note face from a fixed size setting.
const Font &fixed_face(const std::string &size)
{
    if (size == "small") {
        return font::body();
    }
    if (size == "large") {
        return font::xlarge();
    }
    if (size == "xlarge") {
        return font::xxlarge();
    }
    // "medium" and anything unrecognized share the 40 px default face.
    return font::large();
}

// Counts the lines of a face that fit between top and the bottom margin.
int fitting_lines(const Font &face, int top)
{
    // Pitch is the distance to the next line, so only the lines before the
    // last one need it; charging the last its glyph box instead is what fits a
    // fifth 52 px line into the same band.
    const int fits = (kBodyBottom - top - face.height) / face.line_height + 1;
    return fits < 1 ? 1 : fits;
}

// Wraps the note at the size the user chose, or at the largest of the four
// faces that shows the whole note at once when the size is "auto".
Layout layout_note(int top)
{
    const std::string prepared = text::prepare(s_note);
    const int width = canvas::kWidth - 2 * kMargin;
    const std::string size = settings::get().text_size;
    if (size != "auto") {
        const Font &face = fixed_face(size);
        return {&face, text::wrap(face, prepared, width), fitting_lines(face, top)};
    }
    // Largest first, so the first face whose whole note fits wins. Falling out
    // of the loop leaves the smallest face, and that note scrolls.
    const Font *const faces[] = {&font::xxlarge(), &font::xlarge(), &font::large(), &font::body()};
    Layout layout;
    for (const Font *face : faces) {
        layout = {face, text::wrap(*face, prepared, width), fitting_lines(*face, top)};
        if (static_cast<int>(layout.lines.size()) <= layout.visible) {
            break;
        }
    }
    return layout;
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

    // "off" reads as chosen and "No" reads as a fault, and the shared prefix
    // keeps the right edge of the band from jumping between the three.
    const char *link = "No Wi-Fi";
    if (s_radio_off) {
        link = "Wi-Fi off";
    } else if (wifi::connected()) {
        link = "Wi-Fi";
    }
    char right[48];
    const int percent = battery::percent();
    std::snprintf(right, sizeof(right), "%s   %s%s", link,
                  percent >= 0 ? (std::to_string(percent) + "%").c_str() : "--",
                  battery::charging() ? " CHG" : (battery::on_usb() ? " USB" : ""));
    canvas::draw_text_right(font::small(), canvas::kWidth - kMargin, 26, right);
}


// Draws the caption line, if there is one, just under the status separator.
void draw_caption()
{
    if (s_caption.empty()) {
        return;
    }
    canvas::draw_text(font::small(), kMargin, kCaptionTop, text::prepare(s_caption).c_str());
}


// Returns the largest first-line index that still fills the body.
int max_first_line()
{
    return s_total_lines > s_visible_lines ? s_total_lines - s_visible_lines : 0;
}


// Draws the position marker in the right margin. It is the only cue that
// there is more note below, so it is drawn whenever one exists.
void draw_scroll_bar(int top)
{
    const int furthest = max_first_line();
    if (furthest == 0) {
        return;
    }
    const int x = canvas::kWidth - kMargin + kScrollGap;
    const int height = kBodyBottom - top;
    int thumb = height * s_visible_lines / s_total_lines;
    if (thumb < kScrollThumbMin) {
        thumb = kScrollThumbMin;
    }
    canvas::fill_rect(x, top, kScrollWidth, height, true);
    canvas::fill_rect(x + 1, top + 1, kScrollWidth - 2, height - 2, false);
    canvas::fill_rect(x, top + (height - thumb) * s_first_line / furthest, kScrollWidth, thumb, true);
}


// Draws the visible lines of the note and records what scroll() may move.
void draw_note()
{
    const int top = body_top();
    const Layout layout = layout_note(top);
    s_total_lines = static_cast<int>(layout.lines.size());
    s_visible_lines = layout.visible;
    // A caption appearing, or a larger face, can strand the view past the end.
    if (s_first_line > max_first_line()) {
        s_first_line = max_first_line();
    }
    int y = top;
    for (int i = s_first_line; i < s_first_line + s_visible_lines && i < s_total_lines; ++i) {
        canvas::draw_text(*layout.face, kMargin, y, layout.lines[i].c_str());
        y += layout.face->line_height;
    }
    draw_scroll_bar(top);
}


// Draws every band into the canvas.
void draw_all(int level)
{
    canvas::clear();
    draw_caption();
    draw_note();
    draw_status(s_status, level);
}

}  // namespace


void set_note(const std::string &note)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_note = note;
    s_first_line = 0;
}


void set_caption(const std::string &text)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_caption = text;
}


void set_radio_off(bool off)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_radio_off = off;
}


void show(const std::string &status, int level, bool full)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_status = status;
    draw_all(level);
    if (full) {
        display::refresh_full();
    } else {
        display::refresh_partial();
    }
}


void refresh()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    draw_all(-1);
    display::refresh_full();
}


void redraw()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    draw_all(-1);
    display::refresh_partial();
}


bool scrollable()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return max_first_line() > 0;
}


bool scroll(int delta)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    // One line of overlap carries the eye from screen to screen, the way a
    // page-down key does; a clean break loses the reader's place.
    const int step = s_visible_lines > 1 ? s_visible_lines - 1 : 1;
    int target = s_first_line + delta * step;
    if (target < 0) {
        target = 0;
    }
    if (target > max_first_line()) {
        target = max_first_line();
    }
    if (target == s_first_line) {
        return false;
    }
    s_first_line = target;
    draw_all(-1);
    display::refresh_partial();
    return true;
}


void show_message(const std::string &title, const std::vector<std::string> &lines)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    canvas::clear();
    canvas::draw_text(font::title(), kMargin, 18, text::prepare(title).c_str());
    canvas::fill_rect(kMargin, kStatusHeight - 2, canvas::kWidth - 2 * kMargin, 2);

    // The only place the firmware version is shown, and it names the product
    // rather than the device, so a renamed Sticky still says what it runs. The
    // address belongs to whichever caller wants it, so it is not repeated.
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
        std::string(settings::kProductName) + " v" + esp_app_get_description()->version;
    canvas::draw_text(font::small(), kMargin, stamp_top, stamp.c_str());
    display::refresh_full();
}

}  // namespace screen
