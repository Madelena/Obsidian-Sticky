// =============================================================================
// OBSIDIAN CLIENT
// =============================================================================
// Writes notes through the Obsidian Local REST API plugin. Daily mode appends
// via the Periodic Notes companion plugin's /periodic/daily/ route; note mode
// creates a file under a folder. Target and template come from settings.cpp.
#pragma once

#include <string>

namespace obsidian_client {

struct Result {
    bool ok = false;
    std::string target;  // Where the text went: "Daily note" or the file path
    std::string error;   // One-line reason on failure
};

// NOTE SAVER
// Appends to today's daily note or creates a new note, per settings.
Result save(const std::string &text);

// CONNECTION TESTER
// Checks the base URL and key, and in daily mode that the periodic-notes
// route exists.
Result test();

}  // namespace obsidian_client
