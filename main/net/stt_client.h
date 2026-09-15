// =============================================================================
// STT CLIENT
// =============================================================================
// Speech-to-text over the OpenAI-compatible transcription endpoint, which
// Groq, OpenAI, and local Whisper servers all serve. Reads the endpoint from
// settings.cpp and the audio from clip.cpp.
#pragma once

#include <string>

namespace stt_client {

struct Result {
    bool ok = false;
    std::string text;   // Transcript on success
    std::string error;  // One-line reason on failure
};

// TRANSCRIBER
// Uploads the current clip and returns the transcript.
Result transcribe();

// CONNECTION TESTER
// Checks the endpoint and key by listing models on the same base URL.
Result test();

}  // namespace stt_client
