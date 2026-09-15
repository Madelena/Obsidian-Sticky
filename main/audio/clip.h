// =============================================================================
// CLIP
// =============================================================================
// The one recording buffer: PCM samples in PSRAM plus the WAV header the STT
// client streams ahead of them. Filled by pipeline.cpp from pdm_mic.cpp and
// kept after upload so a failed stage can be retried without re-recording.
#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace clip {

constexpr uint32_t kSampleRateHz = 16000;
constexpr uint32_t kMaxSeconds = 90;
constexpr size_t kMaxSamples = kSampleRateHz * kMaxSeconds;
constexpr size_t kWavHeaderSize = 44;

// CLIP INITIALIZER
// Allocates the 2.9 MB sample buffer in PSRAM.
esp_err_t init();

// CLIP RESETTER
// Discards the current recording.
void reset();

// SAMPLE APPENDER
// Copies samples in; returns false once the buffer is full (the recording
// then stops at kMaxSeconds).
bool append(const int16_t *samples, size_t count);

// CLIP ACCESSORS
// Sample count, raw samples, and the length in milliseconds.
size_t sample_count();
const int16_t *samples();
uint32_t duration_ms();

// LEVEL METER
// Returns 0 to 100 from the RMS of the most recent window_samples samples.
int level_percent(size_t window_samples);

// WAV HEADER WRITER
// Fills out with the 44-byte RIFF header for the current sample count.
void write_wav_header(uint8_t *out);

// WAV SIZE GETTER
// Returns header plus PCM bytes, the Content-Length of a full upload.
size_t wav_size();

}  // namespace clip
