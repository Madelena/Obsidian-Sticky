// =============================================================================
// SCREEN
// =============================================================================
// The one screen this device has: a status band, the last note, and a footer.
// pipeline.cpp drives it; it owns the layout, the note paging, and decides
// partial versus full refresh via display.cpp.
#pragma once

#include <string>
#include <vector>

namespace screen {

// NOTE SETTER
// Stores the note text (UTF-8) shown in the body and rewinds to page one.
void set_note(const std::string &note);

// FOOTER SETTER
// Stores the footer message, such as "Saved 14:32 to Daily note".
void set_footer(const std::string &text);

// SCREEN SHOWER
// Redraws status, note, and footer. level 0..100 adds a mic meter to the
// status band; -1 hides it. full forces the slow, clean refresh.
void show(const std::string &status, int level = -1, bool full = false);

// NOTE SCROLLER
// Moves delta pages through a note longer than the body and redraws with the
// last status; returns false when the move is impossible.
bool scroll(int delta);

// MESSAGE SHOWER
// Replaces everything with a title and lines, full refresh. Used for the
// info screen, setup mode, and fatal errors.
void show_message(const std::string &title, const std::vector<std::string> &lines);

}  // namespace screen
