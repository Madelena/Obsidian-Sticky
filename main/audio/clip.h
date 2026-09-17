// =============================================================================
// CLIP
// =============================================================================
// The one recording buffer: a PSRAM ring filled by the capture task in
// pipeline.cpp and released a segment at a time as each one becomes text.
#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace clip {

constexpr uint32_t kSampleRateHz = 16000;
constexpr uint32_t kBacklogSeconds = 90;
constexpr size_t kCapacity = kSampleRateHz * kBacklogSeconds;
constexpr size_t kWavHeaderSize = 44;

constexpr size_t kBlockSamples = 512;  // 32 ms, one read in pipeline.cpp
// 4.1 s of trailing energy. The detector reads this from the pipeline task,
// which a panel refresh can block for most of a second, so the window has to
// outlast a stall by a wide margin or blocks fall out before they are seen.
constexpr size_t kBlockHistory = 128;

// CLIP INITIALIZER
// Allocates the 2.9 MB ring in PSRAM.
esp_err_t init();

// CLIP RESETTER
// Discards the current recording and clears the overflow flag.
void reset();

// SAMPLE APPENDER
// Copies samples in and returns false once the backlog is full, which ends the
// recording and raises overflowed().
bool append(const int16_t *samples, size_t count);

// CLIP ACCESSORS
// recorded() counts every sample ever appended and never goes down, so the
// elapsed time is drawn from it; resident() is only what is still held waiting
// to be transcribed, and shrinks as segments are released.
uint32_t recorded();
uint32_t resident();
uint32_t released();
uint32_t duration_ms();
bool overflowed();

// SEGMENT RELEASER
// Frees everything below offset. Only the consumer may call it, only in order,
// and only once post_wav() has fully returned for that segment, because
// post_wav replays its body on a redirect or a stale socket.
void release_to(uint32_t offset);

// RING RUN READER
// Returns the samples at absolute offset and sets run_len to how many of want
// are contiguous there, which is fewer at the wrap. Reads no shared state, so
// a caller holding a range captured earlier races nothing.
const int16_t *run_at(uint32_t offset, size_t want, size_t &run_len);

// LEVEL METER
// Returns 0 to 100 from the RMS of the most recent window_samples samples.
int level_percent(size_t window_samples);

// BLOCK ENERGY ACCESSORS
// append() folds every kBlockSamples into one RMS value, which is what the
// silence detector in pipeline.cpp cuts segments on. blocks() counts every
// block ever completed, and block_rms() takes one of those absolute numbers
// and returns 0 for a block that has fallen out of the kBlockHistory window.
uint32_t blocks();
uint16_t block_rms(uint32_t index);

// ROOM LEVEL ACCESSORS
// The quietest and loudest this room has been lately, in DC-removed RMS, both
// re-measured from nothing for every recording. The meter scales itself
// between them and the silence detector in pipeline.cpp takes its thresholds
// from the floor, so neither is fitted to one voice, microphone or room.
uint16_t noise_floor();
uint16_t recent_peak();

// WAV HEADER WRITER
// Fills out with the 44-byte RIFF header for a body of data_bytes.
void write_wav_header(uint8_t *out, uint32_t data_bytes);

// WAV SIZE GETTER
// Returns header plus count samples as bytes, one segment's Content-Length.
size_t wav_size(size_t count);

}  // namespace clip
