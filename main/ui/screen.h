// =============================================================================
// SCREEN
// =============================================================================
// The note page: one note, an optional caption, and a status band along the
// foot. pipeline.cpp chooses which note; this owns the layout, the scrolling,
// and the choice of partial versus full refresh via display.cpp.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui/icons.h"

namespace screen {

// NOTE SETTER
// Stores the note text (UTF-8) shown in the body, rewinds to the top, or to
// the last screenful when tail is set, which is what a transcript still being
// written wants, and clears the note time so a caller that forgets to set one
// gets no date rather than the wrong date.
void set_note(const std::string &note, bool tail = false);

// NOTE TIME SETTER
// Stores when the note was recorded, as Unix time, which the status band
// shows as "Today at 9:05" whenever it has no status word. 0 means unknown and
// draws nothing. Set it after set_note(), which resets it.
void set_note_time(int64_t when);

// CAPTION SETTER
// Stores the line shown above the status band, such as an error reason; an
// empty string hides it and gives the space back to the note.
void set_caption(const std::string &text);

// RADIO STATE SETTER
// Stores whether the radio was deliberately stopped, so the status band can
// say "Wi-Fi off" instead of reporting a fault. pipeline.cpp owns the policy
// and is the only caller.
void set_radio_off(bool off);

// SCREEN SHOWER
// Redraws status, caption, and note. level 0..100 adds a mic meter to the
// status band; -1 hides it. full forces the slow, clean refresh.
void show(const std::string &status, int level = -1, bool full = false);

// READY SHOWER
// Redraws with nothing where the status word goes, which is what the device
// shows while it is simply waiting. full forces the slow, clean refresh.
void show_ready(bool full = false);

// SLEEP SHOWER
// Redraws with the crescent moon in place of the status word. full forces the
// slow, clean refresh.
void show_asleep(bool full = false);

// SCREEN REFRESHER
// Refreshes the panel from the stored status, caption, and note, so a
// settings change shows without waiting for the next pipeline event.
void refresh();

// SCREEN REDRAWER
// Repaints the stored status, caption, and note with a partial refresh, for a
// status band change such as Wi-Fi or battery that brings no new headline.
void redraw();

// NOTE OVERFLOW REPORTER
// Reports whether the note drawn last runs past the body, which is what
// pipeline.cpp uses to decide the touch panel is worth powering.
bool scrollable();

// NOTE SCROLLER
// Moves the note body by delta screens, one line of overlap each way, and
// redraws with the last status; returns false when the move is impossible.
bool scroll(int delta);

// SCROLL POSITION REPORTER
// Returns the topmost wrapped line on screen, for a caller that means to come
// back to it. Only meaningful after a paint, which is what measures the wrap.
int first_line();

// SCROLL POSITION SETTER
// Puts the topmost line back where first_line() found it, without painting.
// A line past the end of the note now showing is clamped at the next paint.
void scroll_to(int line);

// One labelled fact on the info screen, drawn as a row of its table.
struct InfoRow {
    std::string label;
    std::string value;
};

// MESSAGE SHOWER
// Replaces everything with a heading and wrapped lines, full refresh. Used
// for setup mode and fatal errors.
void show_message(const std::string &title, const std::vector<std::string> &lines);

// INFO SHOWER
// Replaces everything with a heading, a borderless two column table of the
// rows, and the paragraphs below it after a gap. Full refresh.
void show_info(const std::string &title, const std::vector<InfoRow> &rows,
               const std::vector<std::string> &paragraphs);

}  // namespace screen
