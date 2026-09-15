// =============================================================================
// POWER
// =============================================================================
// Idle tracking and deep-sleep entry. The AI button (GPIO4) is the only wake
// source; on wake the chip reboots into app_main, and board.cpp releases the
// pin holds set here.
#pragma once

#include <cstdint>

namespace power {

// ACTIVITY MARKER
// Restarts the idle timer; call on every button event and pipeline stage.
void note_activity();

// IDLE CHECKER
// Returns true once idle_minutes have passed since the last activity.
// idle_minutes of 0 never expires.
bool idle_expired(int idle_minutes);

// DEEP SLEEPER
// Stops Wi-Fi, holds the latch and peripheral-enable pins, arms the AI
// button as EXT1 wake, and sleeps. The caller must have rendered the screen
// and put the panel to sleep first. Does not return.
[[noreturn]] void deep_sleep();

}  // namespace power
