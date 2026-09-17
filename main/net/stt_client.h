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
    bool retryable = false;  // Transport error, 429 or 5xx: worth another try
    std::string text;        // Transcript on success
    std::string error;       // One-line reason on failure
};

// TRANSCRIBER
// Uploads count samples of the clip.cpp ring from first_sample and returns
// the transcript for that stretch alone. The range must stay resident until
// this returns, because a redirect or a stale socket replays the body.
Result transcribe(uint32_t first_sample, size_t count);

// CONNECTION TESTER
// Checks the endpoint and key by listing models on the same base URL.
Result test();

}  // namespace stt_client
