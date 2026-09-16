// =============================================================================
// SCREEN
// =============================================================================
// Layout of the scrolling note body, an optional caption, and the status band
// along the foot of the 800x480 canvas. The note face comes from the
// text_size setting in settings.cpp, which may ask for the largest face that
// shows the note whole.
#include "ui/screen.h"

#include <mutex>

#include "app/settings.h"
#include "board/battery.h"
#include "esp_app_desc.h"
#include "net/wifi.h"
#include "ui/canvas.h"
#include "ui/display.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/text.h"

namespace screen {
namespace {

constexpr int kMargin = 36;      // Every margin of the note, on three sides
constexpr int kBarMargin = 18;    // Half of it, under the bar, which is chrome
constexpr int kBlockGap = 6;
constexpr int kEdgeGap = 12;      // Clearance between the note and the bar
// The info screen is a titled page rather than a note page, so its heading
// block is sized for the heading and not by the bar at the foot of the other.
constexpr int kTitleHeight = 76;
constexpr int kMessageTop = kTitleHeight + kEdgeGap;
constexpr int kMessageBottom = canvas::kHeight - kEdgeGap;
constexpr int kMeterWidth = 220;
// Three slots of one pitch, drawn from the right margin inwards. The power
// slot is reserved whether or not a cable is in, so the aerial and the cell
// never shift under a change that is not about them.
constexpr int kSlotPitch = icons::kSlotBox + 12;
constexpr int kBatteryCx = canvas::kWidth - kMargin - icons::kSlotBox / 2;
constexpr int kPowerCx = kBatteryCx - kSlotPitch;
constexpr int kLinkCx = kPowerCx - kSlotPitch;
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
bool s_asleep = false;
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

// Puts the status bar where its own glyph box clears the bottom edge. There
// is no band height any more: with no rule under it there was never a band to
// size, only ink to place, and sizing one cost the note seven pixels.
int bar_top()
{
    return canvas::kHeight - kBarMargin - font::title().height;
}

// Puts the caption directly above the status bar, close enough that the bar's
// own word is still read with it.
int caption_top()
{
    return bar_top() - kEdgeGap - font::small().line_height;
}

// Gives the note body its bottom edge, lifted when a caption is showing.
int body_bottom()
{
    return s_caption.empty() ? bar_top() - kEdgeGap : caption_top() - kBlockGap;
}

// Counts the blank rows a face carries above the ink of a capital. Every
// capital and ascender in Atkinson shares that top, so 'H' speaks for the
// whole face.
int cap_gap(const Font &face)
{
    const FontGlyph &glyph = font::glyph(face, 'H');
    const int row_bytes = (glyph.width + 7) / 8;
    for (int y = 0; y < face.height; ++y) {
        const uint8_t *const row = face.bitmap + glyph.offset + y * row_bytes;
        for (int b = 0; b < row_bytes; ++b) {
            if (row[b] != 0) {
                return y;
            }
        }
    }
    return 0;
}

// Gives the y to draw a face at so its ink clears the top of the page by the
// same margin the text clears the sides by. Text is placed by its glyph box,
// and each of the four faces brings a different amount of space of its own.
int margin_top(const Font &face)
{
    return kMargin - cap_gap(face);
}

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

// Counts the lines of a face that fit above whatever bounds the body.
int fitting_lines(const Font &face)
{
    // Pitch is the distance to the next line, so only the lines before the
    // last one need it; charging the last its glyph box instead is what fits a
    // fifth 52 px line into the same band.
    const int fits = (body_bottom() - margin_top(face) - face.height) / face.line_height + 1;
    return fits < 1 ? 1 : fits;
}

// Wraps the note at the size the user chose, or at the largest of the four
// faces that shows the whole note at once when the size is "auto".
Layout layout_note()
{
    const std::string prepared = text::prepare(s_note);
    const int width = canvas::kWidth - 2 * kMargin;
    const std::string size = settings::get().text_size;
    if (size != "auto") {
        const Font &face = fixed_face(size);
        return {&face, text::wrap(face, prepared, width), fitting_lines(face)};
    }
    // Largest first, so the first face whose whole note fits wins. Falling out
    // of the loop leaves the smallest face, and that note scrolls.
    const Font *const faces[] = {&font::xxlarge(), &font::xlarge(), &font::large(), &font::body()};
    Layout layout;
    for (const Font *face : faces) {
        layout = {face, text::wrap(*face, prepared, width), fitting_lines(*face)};
        if (static_cast<int>(layout.lines.size()) <= layout.visible) {
            break;
        }
    }
    return layout;
}

// Draws the status bar: mood or word left, meter or the icon cluster right.
void draw_status(int level)
{
    const int top = bar_top();
    const int mid = top + font::title().height / 2;
    canvas::fill_rect(0, top - kEdgeGap, canvas::kWidth, canvas::kHeight - top + kEdgeGap, false);
    if (s_asleep) {
        icons::draw_asleep(kMargin + icons::kFaceBox / 2, mid);
    } else {
        // An empty status is the resting case and draws nothing at all.
        canvas::draw_text(font::title(), kMargin, top, text::prepare(s_status).c_str());
    }

    if (level >= 0) {
        const int x = canvas::kWidth - kMargin - kMeterWidth;
        canvas::fill_rect(x, mid - 11, kMeterWidth, 22, true);
        canvas::fill_rect(x + 2, mid - 9, kMeterWidth - 4, 18, false);
        canvas::fill_rect(x + 2, mid - 9, (kMeterWidth - 4) * level / 100, 18, true);
        return;
    }

    icons::Link link = icons::Link::Lost;
    if (s_radio_off) {
        link = icons::Link::Off;
    } else if (wifi::connected()) {
        link = icons::Link::Connected;
    }
    icons::draw_link(link, kLinkCx, mid);
    icons::draw_power(battery::charging(), battery::on_usb(), kPowerCx, mid);
    icons::draw_battery(battery::percent(), kBatteryCx, mid);
}


// Draws the caption line, if there is one, just above the status bar.
void draw_caption()
{
    if (s_caption.empty()) {
        return;
    }
    canvas::draw_text(font::small(), kMargin, caption_top(), text::prepare(s_caption).c_str());
}


// Returns the largest first-line index that still fills the body.
int max_first_line()
{
    return s_total_lines > s_visible_lines ? s_total_lines - s_visible_lines : 0;
}


// Draws the position marker in the right margin. It is the only cue that
// there is more note below, so it is drawn whenever one exists.
void draw_scroll_bar()
{
    const int furthest = max_first_line();
    if (furthest == 0) {
        return;
    }
    const int x = canvas::kWidth - kMargin + kScrollGap;
    // The bar is its own ink, with no glyph box around it, so it starts at the
    // margin itself and lines up with the cap of the first line of note.
    const int height = body_bottom() - kMargin;
    int thumb = height * s_visible_lines / s_total_lines;
    if (thumb < kScrollThumbMin) {
        thumb = kScrollThumbMin;
    }
    canvas::fill_rect(x, kMargin, kScrollWidth, height, true);
    canvas::fill_rect(x + 1, kMargin + 1, kScrollWidth - 2, height - 2, false);
    canvas::fill_rect(x, kMargin + (height - thumb) * s_first_line / furthest, kScrollWidth,
                      thumb, true);
}


// Draws the visible lines of the note and records what scroll() may move.
void draw_note()
{
    const Layout layout = layout_note();
    s_total_lines = static_cast<int>(layout.lines.size());
    s_visible_lines = layout.visible;
    // A caption appearing, or a larger face, can strand the view past the end.
    if (s_first_line > max_first_line()) {
        s_first_line = max_first_line();
    }
    int y = margin_top(*layout.face);
    for (int i = s_first_line; i < s_first_line + s_visible_lines && i < s_total_lines; ++i) {
        canvas::draw_text(*layout.face, kMargin, y, layout.lines[i].c_str());
        y += layout.face->line_height;
    }
    draw_scroll_bar();
}


// Draws every band into the canvas.
void draw_all(int level)
{
    canvas::clear();
    draw_caption();
    draw_note();
    draw_status(level);
}


// Draws every band and pushes it to the panel; the caller holds s_mutex.
void paint(int level, bool full)
{
    draw_all(level);
    if (full) {
        display::refresh_full();
    } else {
        display::refresh_partial();
    }
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
    s_asleep = false;
    paint(level, full);
}


void show_ready(bool full)
{
    show("", -1, full);
}


void show_asleep(bool full)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_status.clear();
    s_asleep = true;
    paint(-1, full);
}


void refresh()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    paint(-1, true);
}


void redraw()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    paint(-1, false);
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
    paint(-1, false);
    return true;
}


void show_message(const std::string &title, const std::vector<std::string> &lines)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    canvas::clear();
    canvas::draw_text(font::title(), kMargin, margin_top(font::title()),
                      text::prepare(title).c_str());
    canvas::fill_rect(kMargin, kTitleHeight - 2, canvas::kWidth - 2 * kMargin, 2);

    // The only place the firmware version is shown, and it names the product
    // rather than the device, so a renamed Sticky still says what it runs. The
    // address belongs to whichever caller wants it, so it is not repeated.
    const int stamp_top = kMessageBottom - font::small().height;
    int y = kMessageTop;
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
