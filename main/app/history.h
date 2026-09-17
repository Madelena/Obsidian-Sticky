// =============================================================================
// HISTORY
// =============================================================================
// The ring of saved notes, in its own `notes` NVS partition so the 24 KB one
// shared with settings is untouched. Entries are keyed by a sequence number
// that is never reused, which is what lets history_max change without re-keying.
#pragma once

#include <cstdint>
#include <string>

#include "esp_err.h"

namespace history {

// No entry. Returned by newest_seq() on an empty ring, and held by
// pipeline.cpp for a note on screen that never reached the ring.
constexpr uint32_t kNoSeq = UINT32_MAX;

// Longest note stored, in bytes. A ten minute recording transcribes to roughly
// 9 KB, so this keeps nearly the longest note the device can make; the old
// nvs_set_str path capped at 3900 and silently halved those.
constexpr size_t kNoteBytes = 8192;

struct Entry {
    std::string text;
    int64_t when = 0;  // Unix time recording started, 0 when the clock was unset
    uint32_t seq = kNoSeq;
};

// HISTORY INITIALIZER
// Mounts the `notes` partition, migrates a pre-history `last_note` into the
// ring, and prunes to cap. Call after settings::init() and before the pipeline
// task starts. Returns ESP_OK even when the partition is missing: history then
// reports unavailable and the device records and saves exactly as before.
esp_err_t init(int cap);

// AVAILABILITY REPORTER
// Returns whether the partition mounted. Everything below is a no-op when not.
bool available();

// ENTRY COUNTER
// Returns how many notes are stored, 0 to the current cap.
int count();

// NEWEST SEQUENCE REPORTER
// Returns the sequence of the most recent entry, or kNoSeq on an empty ring.
uint32_t newest_seq();

// OLDEST SEQUENCE REPORTER
// Returns the sequence of the oldest entry kept, or kNoSeq on an empty ring.
uint32_t oldest_seq();

// ENTRY READER
// Fills out with the entry at seq; returns false when it was never written or
// has already been pruned.
bool get(uint32_t seq, Entry &out);

// ENTRY APPENDER
// Stores text stamped with when, truncated to kNoteBytes on a code point
// boundary, prunes back to the cap, and returns the new entry's sequence, or
// kNoSeq when the partition is unavailable or the write failed.
uint32_t append(const std::string &text, int64_t when);

// CAPACITY SETTER
// Prunes immediately to cap, for a history_max lowered from the portal.
void set_capacity(int cap);

}  // namespace history
