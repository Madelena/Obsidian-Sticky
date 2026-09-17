// =============================================================================
// DATETIME
// =============================================================================
// Relative date and time wording for the status band in ui/screen.cpp, which
// calls this at draw time so "Today" cannot go stale past midnight.
#include "ui/datetime.h"

#include <cstdio>
#include <ctime>

#include "app/settings.h"
#include "net/wifi.h"

namespace datetime {
namespace {

// Beyond this the weekday name stops being unambiguous and the date takes over.
constexpr int kWeekdayDays = 7;

// Midnight local to the given time, which is what makes the day count a count
// of calendar days rather than of 24-hour blocks. tm_isdst = -1 leaves mktime
// to resolve the offset, so a day containing a DST change still counts as one.
time_t local_midnight(time_t when)
{
    struct tm local = {};
    localtime_r(&when, &local);
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    return mktime(&local);
}

// Formats the clock part, honouring hour12. Written by hand because
// strftime("%H") always zero-pads and the unpadded %k is a GNU extension.
std::string clock_part(const struct tm &local, bool hour12)
{
    int hour = local.tm_hour;
    const char *suffix = "";
    if (hour12) {
        suffix = hour < 12 ? " am" : " pm";
        hour %= 12;
        if (hour == 0) {
            hour = 12;
        }
    }
    char text[16];
    std::snprintf(text, sizeof(text), "%d:%02d%s", hour, local.tm_min, suffix);
    return text;
}

}  // namespace


std::string relative(int64_t when, bool capitalize)
{
    if (when <= 0 || !wifi::time_synced()) {
        return "";
    }
    const time_t stamp = static_cast<time_t>(when);
    struct tm local = {};
    localtime_r(&stamp, &local);

    const time_t now = time(nullptr);
    // Clamped at zero because SNTP can step the clock backwards after a note
    // was saved, which would otherwise date it in the future.
    long days = (local_midnight(now) - local_midnight(stamp)) / 86400;
    if (days < 0) {
        days = 0;
    }

    std::string day;
    if (days == 0) {
        day = capitalize ? "Today" : "today";
    } else if (days == 1) {
        day = capitalize ? "Yesterday" : "yesterday";
    } else if (days < kWeekdayDays) {
        char text[24];
        std::strftime(text, sizeof(text), "%A", &local);
        day = text;
    } else {
        // Assembled rather than left to strftime, because %e space-pads a
        // single digit and the unpadded forms are extensions this libc need
        // not carry.
        char month[8];
        std::strftime(month, sizeof(month), "%b", &local);
        struct tm now_local = {};
        localtime_r(&now, &now_local);
        char text[24];
        if (now_local.tm_year != local.tm_year) {
            std::snprintf(text, sizeof(text), "%s %d %d", month, local.tm_mday,
                          local.tm_year + 1900);
        } else {
            std::snprintf(text, sizeof(text), "%s %d", month, local.tm_mday);
        }
        day = text;
    }
    return day + " at " + clock_part(local, settings::get().hour12);
}

}  // namespace datetime
