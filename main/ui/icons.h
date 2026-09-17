// =============================================================================
// ICONS
// =============================================================================
// The status band's pictograms, blitted from Material Symbols outlines baked
// to 1-bit by tools/gen_icons.py into the generated ui/icons_data.h.
#pragma once

#include <cstdint>

struct Icon {
    uint16_t offset;    // Byte offset into icon_bitmap
    uint8_t width;      // Ink width in pixels, rows are (width + 7) / 8 bytes
    uint8_t height;     // Ink height in pixels
};

namespace icons {

// The em square each icon is centered in, which is what sets the slot pitch
// in screen.cpp. Baked ink is smaller by the glyph's own side bearing, and
// these must match SLOT_PX and BATTERY_PX in tools/gen_icons.py.
constexpr int kSlotBox = 34;
constexpr int kBatteryBox = 52;

// What the radio is doing, which is not the same question as whether it is
// connected: a stopped radio is a choice and a lost link is a fault.
enum class Link {
    Connected,
    Off,
    Lost,
};

// SLEEP MARK DRAWER
// Draws the crescent moon centered on the given point, which is the leftmost
// of the right-hand cluster. Ready has no mark of its own: an empty bar is
// the calmest thing the device can show.
void draw_asleep(int center_x, int center_y);

// RADIO LINK DRAWER
// Draws the aerial plain when connected, struck through when the radio was
// stopped, and crossed when the link should be up and is not.
void draw_link(Link link, int center_x, int center_y);

// CHARGE MARK DRAWER
// Draws the bolt, which says charge is flowing into the pack.
void draw_charging(int center_x, int center_y);

// BATTERY GAUGE DRAWER
// Draws the cell at one of seven fills, a warning mark below ten percent, or
// a question mark when percent is negative and the gauge did not answer.
void draw_battery(int percent, int center_x, int center_y);

// BATTERY STEP COUNTER
// Returns which of the nine gauge drawings a percentage lands on, 0 for the
// warning mark up to 7 for a full cell and -1 for an unreadable gauge, so
// pipeline.cpp can repaint on a change the user can see rather than on every
// percent the fuel gauge reports.
int battery_step(int percent);

}  // namespace icons
