// =============================================================================
// PIPELINE
// =============================================================================
// The application loop: button events in, recording, transcription, cleanup,
// save, and every screen update. Runs as its own task so TLS has stack room;
// main.cpp starts it after the hardware is up.
#pragma once

#include "esp_err.h"

namespace pipeline {

// PIPELINE STARTER
// Creates the pipeline task. When the boot was a button wake and the button
// is still held, recording starts before Wi-Fi is connected.
esp_err_t start();

}  // namespace pipeline
