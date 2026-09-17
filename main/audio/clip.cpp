// =============================================================================
// CLIP
// =============================================================================
// PSRAM ring accounting and WAV header builder.
#include "audio/clip.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "esp_heap_caps.h"

namespace clip {
namespace {

int16_t *s_samples = nullptr;

// The two ends of the ring, each with exactly one writer: append() on the
// capture task moves s_written, and release_to() on the consumer moves
// s_freed. Counting samples ever seen rather than a physical index is what
// keeps recorded() monotonic for the elapsed time; a uint32_t of samples runs
// 74 hours before it wraps. They are deliberately not uint64_t, because the
// Xtensa 64-bit atomics in ESP-IDF take a critical section on every access.
std::atomic<uint32_t> s_written{0};
std::atomic<uint32_t> s_freed{0};
std::atomic<bool> s_overflowed{false};

// Bounds on the tracked room levels, wide enough to be no more than a sanity
// check. Full scale is 32768 and no voice this microphone hears comes near
// it: a silent room measures under 100 and ordinary speech a few hundred.
constexpr uint32_t kFloorMinRms = 20;
constexpr uint32_t kFloorMaxRms = 2000;
constexpr uint32_t kPeakMinRms = 80;
constexpr uint32_t kFloorRiseQ8 = 8;  // About 1 RMS per second

// The bar runs from a little above the room's own noise to the loudest the
// room has been. Three floors up keeps silence reading as empty, and the
// minimum span stops the bar swinging wildly on nothing before anyone speaks.
constexpr uint32_t kMeterHeadroom = 3;
constexpr uint32_t kMeterMinSpan = 4;

// Trailing energy, one value per kBlockSamples, folded in as the samples go
// past rather than walked afterwards: the detector needs a continuous view,
// and the capture task already has these samples in cache. s_block_sum and
// s_block_fill are touched only by append(), so they need no atomics.
uint16_t s_block_rms[kBlockHistory] = {};
std::atomic<uint32_t> s_blocks{0};
// Both in 1/256 RMS. Written only by fold_energy on the capture task; readers
// want a recent value rather than a synchronised one, so relaxed is enough.
std::atomic<uint32_t> s_floor_q8{kFloorMaxRms << 8};
std::atomic<uint32_t> s_peak_q8{kPeakMinRms << 8};
int64_t s_block_sum = 0;   // Sum of samples, which gives the block's DC level
uint64_t s_block_sq = 0;   // Sum of squares
size_t s_block_fill = 0;

// Folds count samples into the block energy ring, publishing each block as it
// completes. Called only from append(), on the capture task.
void fold_energy(const int16_t *samples, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const int32_t sample = samples[i];
        s_block_sum += sample;
        s_block_sq += static_cast<uint64_t>(sample * sample);
        if (++s_block_fill < kBlockSamples) {
            continue;
        }
        // Take the DC level out before the RMS. This microphone sits on an
        // offset near 1300, and sqrt(DC^2 + AC^2) is dominated by it: a
        // silent room measured 1308 and speech only 1528, so a meter drawn
        // from the raw RMS cannot move and a threshold cannot see a pause.
        const int64_t mean = s_block_sum / static_cast<int64_t>(kBlockSamples);
        const int64_t mean_sq = static_cast<int64_t>(s_block_sq / kBlockSamples);
        const int64_t variance = mean_sq - mean * mean;
        const uint32_t done = s_blocks.load(std::memory_order_relaxed);
        const uint16_t rms =
            variance > 0 ? static_cast<uint16_t>(std::sqrt(static_cast<float>(variance))) : 0;
        s_block_rms[done % kBlockHistory] = rms;
        s_blocks.store(done + 1, std::memory_order_release);

        // The pair brackets the voice: fast down and slow up for the floor,
        // fast up and slow down for the ceiling. So a burst cannot drag the
        // floor with it, and a pause cannot drop the ceiling out from under
        // the bar. The ceiling halves over about eleven seconds.
        const uint32_t rms_q8 = static_cast<uint32_t>(rms) << 8;
        const uint32_t floor = s_floor_q8.load(std::memory_order_relaxed);
        s_floor_q8.store(rms_q8 < floor ? rms_q8 : floor + kFloorRiseQ8,
                         std::memory_order_relaxed);
        const uint32_t peak = s_peak_q8.load(std::memory_order_relaxed);
        const uint32_t decay = std::max<uint32_t>(1, peak >> 9);
        s_peak_q8.store(rms_q8 > peak ? rms_q8 : (peak > decay ? peak - decay : 0),
                        std::memory_order_relaxed);
        s_block_sum = 0;
        s_block_sq = 0;
        s_block_fill = 0;
    }
}

// Writes a little-endian 32-bit value.
void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
    out[2] = static_cast<uint8_t>(value >> 16);
    out[3] = static_cast<uint8_t>(value >> 24);
}

// Writes a little-endian 16-bit value.
void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}

}  // namespace


esp_err_t init()
{
    if (s_samples != nullptr) {
        return ESP_OK;
    }
    s_samples = static_cast<int16_t *>(
        heap_caps_malloc(kCapacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return s_samples != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
}


void reset()
{
    s_written.store(0, std::memory_order_relaxed);
    s_freed.store(0, std::memory_order_relaxed);
    s_overflowed.store(false, std::memory_order_relaxed);
    s_blocks.store(0, std::memory_order_relaxed);
    s_floor_q8.store(kFloorMaxRms << 8, std::memory_order_relaxed);
    s_peak_q8.store(kPeakMinRms << 8, std::memory_order_relaxed);
    s_block_sum = 0;
    s_block_sq = 0;
    s_block_fill = 0;
}


bool append(const int16_t *samples, size_t count)
{
    if (s_samples == nullptr || count == 0) {
        return false;
    }
    // Relaxed on our own end of the ring, acquire on the consumer's: this runs
    // only on the capture task, so nothing else moves s_written.
    const uint32_t written = s_written.load(std::memory_order_relaxed);
    const uint32_t room = kCapacity - (written - s_freed.load(std::memory_order_acquire));
    const size_t take = std::min(count, static_cast<size_t>(room));
    const size_t head = written % kCapacity;
    const size_t first = std::min(take, kCapacity - head);
    std::memcpy(s_samples + head, samples, first * sizeof(int16_t));
    if (take > first) {
        std::memcpy(s_samples, samples + first, (take - first) * sizeof(int16_t));
    }
    // Release pairs with the acquire in every reader: the copies above must be
    // visible before the count that advertises them, and the capture task and
    // the uploader genuinely run on different cores.
    s_written.store(written + static_cast<uint32_t>(take), std::memory_order_release);
    fold_energy(samples, take);
    if (take != count) {
        s_overflowed.store(true, std::memory_order_relaxed);
        return false;
    }
    return true;
}


uint32_t recorded()
{
    return s_written.load(std::memory_order_acquire);
}


uint32_t resident()
{
    const uint32_t written = s_written.load(std::memory_order_acquire);
    return written - s_freed.load(std::memory_order_acquire);
}


uint32_t released()
{
    return s_freed.load(std::memory_order_acquire);
}


bool overflowed()
{
    return s_overflowed.load(std::memory_order_relaxed);
}


void release_to(uint32_t offset)
{
    s_freed.store(offset, std::memory_order_release);
}


const int16_t *run_at(uint32_t offset, size_t want, size_t &run_len)
{
    const size_t head = offset % kCapacity;
    run_len = std::min(want, kCapacity - head);
    return s_samples + head;
}


uint32_t duration_ms()
{
    return static_cast<uint32_t>(recorded() * 1000ULL / kSampleRateHz);
}


int level_percent(size_t window_samples)
{
    const uint32_t done = s_blocks.load(std::memory_order_acquire);
    if (done == 0 || window_samples < kBlockSamples) {
        return 0;
    }
    const size_t want = window_samples / kBlockSamples;
    const size_t n = std::min({want, static_cast<size_t>(done), kBlockHistory - 1});
    // Loudest block in the window rather than the average of them, because a
    // bar that answers syllables is what says the microphone is live.
    uint16_t peak = 0;
    for (size_t i = 0; i < n; ++i) {
        peak = std::max(peak, s_block_rms[(done - 1 - i) % kBlockHistory]);
    }
    // Logarithmic, because hearing is, and between the room's own levels
    // rather than fixed ends. A constant scale has to be fitted to one voice
    // at one distance in one room, and is wrong everywhere else: this
    // microphone reads a whisper at 139 and ordinary speech at 466, both of
    // which look like nothing against full scale.
    const uint32_t bottom = static_cast<uint32_t>(noise_floor()) * kMeterHeadroom;
    const uint32_t top = std::max<uint32_t>(recent_peak(), bottom * kMeterMinSpan);
    int percent = 0;
    if (peak > bottom) {
        const float span = std::log(static_cast<float>(top) / static_cast<float>(bottom));
        const float here = std::log(static_cast<float>(peak) / static_cast<float>(bottom));
        percent = std::clamp(static_cast<int>(here * 100.0F / span), 0, 100);
    }
    return percent;
}


uint32_t blocks()
{
    return s_blocks.load(std::memory_order_acquire);
}


uint16_t noise_floor()
{
    const uint32_t floor = s_floor_q8.load(std::memory_order_relaxed) >> 8;
    return static_cast<uint16_t>(std::clamp(floor, kFloorMinRms, kFloorMaxRms));
}


uint16_t recent_peak()
{
    const uint32_t peak = s_peak_q8.load(std::memory_order_relaxed) >> 8;
    return static_cast<uint16_t>(std::max(peak, kPeakMinRms));
}


uint16_t block_rms(uint32_t index)
{
    const uint32_t done = s_blocks.load(std::memory_order_acquire);
    // The oldest retained block shares its slot with the one being written
    // next, so the window is one short of kBlockHistory rather than equal.
    if (index >= done || done - index >= kBlockHistory) {
        return 0;
    }
    return s_block_rms[index % kBlockHistory];
}


void write_wav_header(uint8_t *out, uint32_t data_bytes)
{
    std::memcpy(out, "RIFF", 4);
    put_u32(out + 4, 36 + data_bytes);
    std::memcpy(out + 8, "WAVEfmt ", 8);
    put_u32(out + 16, 16);
    put_u16(out + 20, 1);
    put_u16(out + 22, 1);
    put_u32(out + 24, kSampleRateHz);
    put_u32(out + 28, kSampleRateHz * sizeof(int16_t));
    put_u16(out + 32, sizeof(int16_t));
    put_u16(out + 34, 16);
    std::memcpy(out + 36, "data", 4);
    put_u32(out + 40, data_bytes);
}


size_t wav_size(size_t count)
{
    return kWavHeaderSize + count * sizeof(int16_t);
}

}  // namespace clip
