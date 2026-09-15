// =============================================================================
// PDM MIC
// =============================================================================
// Capture from the onboard PDM microphone as 16 kHz, 16-bit mono PCM. The
// pipeline task in pipeline.cpp calls start(), read() in a loop, then stop();
// samples go into clip.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace pdm_mic {

constexpr uint32_t kSampleRateHz = 16000;

// MIC INITIALIZER
// Frees GPIO19/20 from the USB-Serial-JTAG pad, configures the mic power pin
// off, and creates the I2S PDM receive channel.
esp_err_t init();

// CAPTURE STARTER
// Powers the microphone, enables the channel, and discards the power-on
// transient so the first samples returned are clean.
esp_err_t start();

// SAMPLE READER
// Blocks up to timeout_ms for up to max_samples; sets got to the count read.
esp_err_t read(int16_t *dest, size_t max_samples, size_t &got, uint32_t timeout_ms);

// CAPTURE STOPPER
// Disables the channel and powers the microphone off.
esp_err_t stop();

}  // namespace pdm_mic
