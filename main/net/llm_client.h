// =============================================================================
// LLM CLIENT
// =============================================================================
// Optional transcript cleanup through either the Anthropic Messages API or an
// OpenAI-compatible chat completions endpoint, chosen in settings.cpp.
#pragma once

#include <string>

namespace llm_client {

struct Result {
    bool ok = false;
    std::string text;   // Cleaned transcript on success
    std::string error;  // One-line reason on failure
};

// TRANSCRIPT CLEANER
// Sends the transcript with the cleanup prompt and returns the model's text.
Result clean(const std::string &transcript);

// CONNECTION TESTER
// Sends a one-word prompt to confirm the endpoint, model, and key work.
Result test();

}  // namespace llm_client
