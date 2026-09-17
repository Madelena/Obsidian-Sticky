// =============================================================================
// SCREEN
// =============================================================================
// Layout of the scrolling note body, an optional caption, and the status band
// along the foot of the 800x480 canvas. The note face comes from the
// text_size setting in settings.cpp, which may ask for the largest face that
// shows the note whole.
#include "ui/screen.h"

#include <algorithm>
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
// Titled pages: the info screen, setup mode and the fatal ones. They are not
// note pages, so they own their own rhythm rather than the bar's.
constexpr int kParagraphGap = 24;    // Under the heading, and under the table
constexpr int kLabelGutter = 28;     // Between the two columns of the table
// A row of cells rather than one filling bar, because a bar that fills from
// the left reads as progress towards something and a recording is not going
// anywhere. Tall and narrow so the row scans as a level.
constexpr int kMeterCells = 20;
constexpr int kMeterCellWidth = 5;
constexpr int kMeterCellHeight = 26;
constexpr int kMeterCellGap = 6;
constexpr int kMeterWidth = kMeterCells * kMeterCellWidth + (kMeterCells - 1) * kMeterCellGap;
// Three slots of one pitch, drawn from the right margin inwards. The power
// slot is reserved whether or not a cable is in, so the aerial and the cell
// never shift under a change that is not about them.
constexpr int kIconGap = 12;
constexpr int kSlotPitch = icons::kSlotBox + kIconGap;
// The cell is drawn in a wider box than the rest, so the first step in from
// the margin is half of each box plus the gap rather than a whole pitch.
constexpr int kBatteryStep = (icons::kBatteryBox + icons::kSlotBox) / 2 + kIconGap;
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
bool s_pin_to_end = false;  // Jump to the last screenful at the next paint
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

// Counts the blank rows a face carries above the ink of a capital, so the note
// margin measures to the cap rather than to the glyph box. Ascenders reach 1 to
// 3 px higher than 'H' in both families, which does not show at a 36 px margin.
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

// One meter cell, filled or hollow, with its four corner pixels left out.
// That is as much rounding as five pixels of width can show, and it is enough
// to stop the row reading as a row of bricks.
void draw_meter_cell(int x, int y, bool filled)
{
    const int w = kMeterCellWidth;
    const int h = kMeterCellHeight;
    if (filled) {
        canvas::fill_rect(x, y + 1, w, h - 2);
        canvas::fill_rect(x + 1, y, w - 2, h);
        return;
    }
    canvas::fill_rect(x + 1, y, w - 2, 1);
    canvas::fill_rect(x + 1, y + h - 1, w - 2, 1);
    canvas::fill_rect(x, y + 1, 1, h - 2);
    canvas::fill_rect(x + w - 1, y + 1, 1, h - 2);
}


// Draws the status bar: mood or word left, meter or the icon cluster right.
void draw_status(int level)
{
    const int top = bar_top();
    const int mid = top + font::title().height / 2;
    canvas::fill_rect(0, top - kEdgeGap, canvas::kWidth, canvas::kHeight - top + kEdgeGap, false);
    // An empty status is the resting case and draws nothing at all. Sleeping
    // is a mark in the cluster below, not a word, so the left of the bar is
    // free for whatever the status has to say.
    canvas::draw_text(font::title(), kMargin, top, text::prepare(s_status).c_str());

    if (level >= 0) {
        const int left = canvas::kWidth - kMargin - kMeterWidth;
        const int top_y = mid - kMeterCellHeight / 2;
        // Anything audible lights the first cell, so "hearing you faintly"
        // never looks the same as "hearing nothing at all".
        const int lit = level <= 0 ? 0 : std::max(1, level * kMeterCells / 100);
        for (int cell = 0; cell < kMeterCells; ++cell) {
            draw_meter_cell(left + cell * (kMeterCellWidth + kMeterCellGap), top_y, cell < lit);
        }
        return;
    }

    icons::Link link = icons::Link::Lost;
    if (s_radio_off) {
        link = icons::Link::Off;
    } else if (wifi::connected()) {
        link = icons::Link::Connected;
    }
    // Packed against the right margin rather than laid into fixed slots, so a
    // mark with nothing to say closes the gap up instead of leaving a hole in
    // the middle of the row. The cell keeps the margin and the rest follow it.
    //
    // There is no mark for a cable on its own. USB is the only way to charge
    // this board, so a trident beside a bolt said the same thing twice, and by
    // itself it could not say what it looked like it said: the pins that would
    // see a data connection are the microphone's.
    int cx = canvas::kWidth - kMargin - icons::kBatteryBox / 2;
    icons::draw_battery(battery::percent(), cx, mid);
    cx -= kBatteryStep;
    if (battery::charging()) {
        icons::draw_charging(cx, mid);
        cx -= kSlotPitch;
    }
    icons::draw_link(link, cx, mid);
    if (s_asleep) {
        cx -= kSlotPitch;
        icons::draw_asleep(cx, mid);
    }
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
    // Only here can the last screen be worked out, because only here has the
    // note been wrapped against the face that will actually draw it.
    if (s_pin_to_end) {
        s_pin_to_end = false;
        s_first_line = max_first_line();
    }
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


// Gives the y below which a titled page must not draw body text, which is
// where the stamp sits. It takes the same bottom margin as the status bar,
// being the same thing: one line of chrome on the floor of the page.
int stamp_top()
{
    return canvas::kHeight - kBarMargin - font::small().height;
}

// Draws the heading of a titled page and returns the y its body starts at.
// There is no rule under it: the note page dropped its own, and a size step
// this large separates the two well enough on its own.
int draw_heading(const std::string &title)
{
    const int top = margin_top(font::heading());
    canvas::draw_text(font::heading(), kMargin, top, text::prepare(title).c_str());
    return top + font::heading().height + kParagraphGap;
}

// Draws one wrapped block at x, indenting every line after the first to the
// same x, and returns the y the next block starts at.
int draw_paragraph(const std::string &text, int x, int width, int y)
{
    for (const std::string &line : text::wrap(font::body(), text::prepare(text), width)) {
        if (y + font::body().height > stamp_top() - kBlockGap) {
            break;
        }
        canvas::draw_text(font::body(), x, y, line.c_str());
        y += font::body().line_height;
    }
    return y;
}

// Stamps the product name and firmware version at the foot of a titled page.
// It names the product rather than the device, so a renamed Sticky still says
// what it runs.
void draw_stamp()
{
    const std::string stamp =
        std::string(settings::kProductName) + " v" + esp_app_get_description()->version;
    canvas::draw_text(font::small(), kMargin, stamp_top(), stamp.c_str());
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


void set_note(const std::string &note, bool tail)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_note = note;
    s_first_line = 0;
    s_pin_to_end = tail;
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
    s_pin_to_end = false;
    paint(-1, false);
    return true;
}


void show_message(const std::string &title, const std::vector<std::string> &lines)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    canvas::clear();
    int y = draw_heading(title);
    for (const std::string &line : lines) {
        y = draw_paragraph(line, kMargin, canvas::kWidth - 2 * kMargin, y);
    }
    draw_stamp();
    display::refresh_full();
}


void show_info(const std::string &title, const std::vector<InfoRow> &rows,
               const std::vector<std::string> &paragraphs)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    canvas::clear();
    int y = draw_heading(title);

    // One column for every label, set by the widest of them, so the values
    // line up without a rule to carry the eye across.
    int column = 0;
    for (const InfoRow &row : rows) {
        const int width = font::text_width(font::body(), text::prepare(row.label).c_str());
        column = width > column ? width : column;
    }
    column += kMargin + kLabelGutter;
    for (const InfoRow &row : rows) {
        if (y + font::body().height > stamp_top() - kBlockGap) {
            break;
        }
        canvas::draw_text(font::body(), kMargin, y, text::prepare(row.label).c_str());
        y = draw_paragraph(row.value, column, canvas::kWidth - kMargin - column, y);
    }

    y += kParagraphGap;
    for (const std::string &line : paragraphs) {
        y = draw_paragraph(line, kMargin, canvas::kWidth - 2 * kMargin, y);
    }
    draw_stamp();
    display::refresh_full();
}

}  // namespace screen
