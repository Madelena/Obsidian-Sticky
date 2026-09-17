// =============================================================================
// DATETIME
// =============================================================================
// Turns a note's recording time into the line the status band shows. Separate
// from ui/text.h because it reads the clock and the hour12 setting rather than
// transforming a string it is handed.
#pragma once

#include <cstdint>
#include <string>

namespace datetime {

// RELATIVE STAMP FORMATTER
// Formats when as "today at 9:05", "yesterday at 14:02", "Wednesday at 9:05",
// "Sep 10 at 9:05" beyond a week, or "Sep 10 2025 at 9:05" in another year,
// honouring the hour12 setting. Capitalize leads with "Today"; leave it off
// behind a word such as "Saved".
//
// Returns "" for a when of 0 and for a clock that never synced, which is how a
// note recorded before SNTP landed draws no date at all rather than Jan 1 1970.
std::string relative(int64_t when, bool capitalize);

}  // namespace datetime
