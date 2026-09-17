// =============================================================================
// BUZZER
// =============================================================================
// Short audible cues for the eyes-free flow: record start, latched, stop,
// saved, error. All calls block for the duration of the cue, so call them from
// the pipeline task, never from a button callback.
#pragma once

#include <cstdint>

#include "esp_err.h"

namespace buzzer {

// BUZZER INITIALIZER
// Configures the LEDC channel on the buzzer pin, silent.
esp_err_t init();

// BUZZER ENABLER
// Mutes every cue when false; the user's "beep" setting drives this.
void set_enabled(bool enabled);

// TONE PLAYER
// Plays one tone for the given duration, then silence.
void beep(uint32_t frequency_hz, uint32_t duration_ms);

// CUE PLAYERS
// Fixed patterns: rising pair for record start, one short high blip when a tap
// latches the recording and the hand can come off, single for stop, high
// double for saved, low long for error.
void cue_start();
void cue_latched();
void cue_stop();
void cue_saved();
void cue_error();

}  // namespace buzzer
