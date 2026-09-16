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

// Blits one baked icon, placing its ink by the offsets the generator stored
// rather than by its own bounds, which is what keeps the states of a family
// on the same line however much ink each of them has.
void blit(IconId id, int center_x, int center_y)
{
    const Icon &icon = icon_table[id];
    const int left = center_x + icon.x_offset;
    const int top = center_y + icon.y_offset;
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


void draw_power(bool charging, bool on_usb, int center_x, int center_y)
{
    if (charging) {
        blit(kIconBolt, center_x, center_y);
    } else if (on_usb) {
        blit(kIconPlug, center_x, center_y);
    }
}


void draw_battery(int percent, int center_x, int center_y)
{
    static const IconId kSteps[] = {
        kIconBatteryLow, kIconBattery25, kIconBattery50, kIconBattery75, kIconBatteryFull,
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
    // Nearest quarter, so the thresholds are the midpoints 87.5, 62.5 and
    // 37.5 taken up to the next whole percent the gauge can report.
    return percent >= 88 ? 4 : percent >= 63 ? 3 : percent >= 38 ? 2 : 1;
}

}  // namespace icons
