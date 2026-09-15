// =============================================================================
// CLIP
// =============================================================================
// PSRAM-backed PCM buffer and WAV header builder.
#include "audio/clip.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esp_heap_caps.h"

namespace clip {
namespace {

int16_t *s_samples = nullptr;
size_t s_count = 0;

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
        heap_caps_malloc(kMaxSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return s_samples != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
}


void reset()
{
    s_count = 0;
}


bool append(const int16_t *samples, size_t count)
{
    if (s_samples == nullptr || s_count >= kMaxSamples) {
        return false;
    }
    const size_t room = kMaxSamples - s_count;
    const size_t take = std::min(count, room);
    std::memcpy(s_samples + s_count, samples, take * sizeof(int16_t));
    s_count += take;
    return take == count;
}


size_t sample_count()
{
    return s_count;
}


const int16_t *samples()
{
    return s_samples;
}


uint32_t duration_ms()
{
    return static_cast<uint32_t>(s_count * 1000ULL / kSampleRateHz);
}


int level_percent(size_t window_samples)
{
    if (s_count == 0 || window_samples == 0) {
        return 0;
    }
    const size_t n = std::min(window_samples, s_count);
    const int16_t *start = s_samples + (s_count - n);
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += static_cast<double>(start[i]) * start[i];
    }
    // Speech at a normal distance peaks around an RMS of 4000 on this mic.
    const double rms = std::sqrt(sum / n);
    return std::clamp(static_cast<int>(rms * 100.0 / 4000.0), 0, 100);
}


void write_wav_header(uint8_t *out)
{
    const uint32_t data_bytes = static_cast<uint32_t>(s_count * sizeof(int16_t));
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


size_t wav_size()
{
    return kWavHeaderSize + s_count * sizeof(int16_t);
}

}  // namespace clip
