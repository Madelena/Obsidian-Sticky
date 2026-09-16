// =============================================================================
// SETTINGS
// =============================================================================
// Every user-configurable value, persisted in NVS and edited only through the
// setup portal (portal.cpp). Readers take a copy with get(); the portal task
// writes with save(), so no other module holds a reference across a call.
#pragma once

#include <string>

#include "esp_err.h"

namespace settings {

// The firmware's own name. It is the default device name, and the info screen
// stamps it under whatever the device has been renamed to, so a screen always
// says what is running on it.
constexpr const char *kProductName = "Obsidian Sticky";

struct Values {
    std::string wifi_ssid;
    std::string wifi_pass;

    std::string stt_url;      // OpenAI-compatible /audio/transcriptions endpoint
    std::string stt_model;
    std::string stt_key;
    std::string stt_lang;     // ISO-639-1 or empty for auto-detect

    bool llm_on = false;
    std::string llm_kind;     // "anthropic" or "openai"
    std::string llm_url;
    std::string llm_model;
    std::string llm_key;
    std::string llm_prompt;

    std::string obs_url;      // Scheme, host and port of the Local REST API
    std::string obs_key;
    std::string obs_mode;     // "daily" or "note"
    std::string obs_folder;   // New-note mode only
    std::string obs_line;     // Daily-mode template with {time} and {text}

    std::string device_name;  // Heading on the info screen, never empty
    std::string text_size;    // Note text: "auto", "small", "medium", "large" or "xlarge"
    std::string text_font;    // Latin face: "inter" or "atkinson". Chinese ignores this.
    std::string tz;           // POSIX TZ string
    int sleep_min = 10;       // Idle minutes before deep sleep, 0 disables
    int wifi_idle_min = 0;    // Idle minutes before the radio stops, 0 disables
    bool beep = true;
};

// SETTINGS INITIALIZER
// Loads NVS values over the defaults; call after nvs_flash_init().
esp_err_t init();

// SETTINGS GETTER
// Returns a snapshot of the current values.
Values get();

// SETTINGS SAVER
// Persists values to NVS and makes them current.
esp_err_t save(const Values &values);

// JSON EXPORTER
// Serializes for the portal. Secrets are replaced by *_set booleans.
std::string to_json();

// JSON IMPORTER
// Merges a portal JSON body into the current values and saves. Empty secret
// fields keep their stored value. Returns false with error filled on bad input.
bool apply_json(const char *json, std::string &error);

// WI-FI CHECKER
// Returns true when an SSID is stored.
bool wifi_configured();

}  // namespace settings
