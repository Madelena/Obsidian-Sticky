// =============================================================================
// ICONS
// =============================================================================
// Blits the baked Material Symbols bitmaps and maps each device state onto
// one of them.
#include "ui/icons.h"

#include "ui/canvas.h"
#include "ui/icons_data.h"

namespace icons {
namespace {

// Blits one baked icon centered on its own ink, which is what puts every mark
// in the bar on one optical line. Centering their em squares instead left the
// inks up to 2.5 px apart, because a glyph sits where its own design says.
void blit(IconId id, int center_x, int center_y)
{
    const Icon &icon = icon_table[id];
    const int left = center_x - icon.width / 2;
    const int top = center_y - icon.height / 2;
    const int row_bytes = (icon.width + 7) / 8;
    const uint8_t *const rows = icon_bitmap + icon.offset;
    for (int y = 0; y < icon.height; ++y) {
        for (int x = 0; x < icon.width; ++x) {
            if ((rows[y * row_bytes + (x >> 3)] & (0x80U >> (x & 7))) != 0) {
                canvas::set_pixel(left + x, top + y, true);
            }
        }
    }
}

}  // namespace


void draw_asleep(int center_x, int center_y)
{
    blit(kIconBedtime, center_x, center_y);
}


void draw_link(Link link, int center_x, int center_y)
{
    IconId id = kIconWifiBad;
    if (link == Link::Connected) {
        id = kIconWifi;
    } else if (link == Link::Off) {
        id = kIconWifiOff;
    }
    blit(id, center_x, center_y);
}


void draw_charging(int center_x, int center_y)
{
    blit(kIconBolt, center_x, center_y);
}


void draw_battery(int percent, int center_x, int center_y)
{
    // Indexed by battery_step(), so the order here is the order of its return.
    static const IconId kSteps[] = {
        kIconBatteryLow, kIconBattery1, kIconBattery2, kIconBattery3,
        kIconBattery4,   kIconBattery5, kIconBattery6, kIconBatteryFull,
    };
    const int step = battery_step(percent);
    blit(step < 0 ? kIconBatteryUnknown : kSteps[step], center_x, center_y);
}


int battery_step(int percent)
{
    if (percent < 0) {
        return -1;
    }
    if (percent < 10) {
        return 0;
    }
    // The family draws seven fills, i/7 full for step i, so the nearest one is
    // percent scaled to sevenths with the +50 rounding the halves up. Ten
    // percent lands on 1, which is why the warning mark takes everything under
    // it rather than sharing a step.
    return (percent * 7 + 50) / 100;
}

}  // namespace icons
