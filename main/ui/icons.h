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
    int8_t x_offset;    // Ink left edge relative to the center a caller passes
    int8_t y_offset;    // Ink top edge relative to that same center
};

namespace icons {

// The em square each icon is centered in, which is what sets the slot pitch
// in screen.cpp. Baked ink is smaller by the glyph's own side bearing, and
// both numbers must match FACE_PX and SLOT_PX in tools/gen_icons.py.
constexpr int kFaceBox = 40;
constexpr int kSlotBox = 34;

// What the radio is doing, which is not the same question as whether it is
// connected: a stopped radio is a choice and a lost link is a fault.
enum class Link {
    Connected,
    Off,
    Lost,
};

// SLEEP MARK DRAWER
// Draws the crescent moon centered on the given point. Ready has no mark of
// its own: an empty band is the calmest thing the device can show.
void draw_asleep(int center_x, int center_y);

// RADIO LINK DRAWER
// Draws the aerial plain when connected, struck through when the radio was
// stopped, and crossed when the link should be up and is not.
void draw_link(Link link, int center_x, int center_y);

// POWER SOURCE DRAWER
// Draws a bolt while charge flows into the pack, a plug while a cable is in
// and it does not, and nothing at all on battery.
void draw_power(bool charging, bool on_usb, int center_x, int center_y);

// BATTERY GAUGE DRAWER
// Draws the cell at one of four quarters, a warning mark below ten percent,
// or a question mark when percent is negative and the gauge did not answer.
void draw_battery(int percent, int center_x, int center_y);

// BATTERY STEP COUNTER
// Returns which of the five gauge drawings a percentage lands on, so
// pipeline.cpp can repaint on a change the user can see rather than on every
// percent the fuel gauge reports.
int battery_step(int percent);

}  // namespace icons
