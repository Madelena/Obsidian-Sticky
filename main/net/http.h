// =============================================================================
// HTTP
// =============================================================================
// Thin wrapper over esp_http_client for the three API clients: one call per
// request, the whole response body captured, and a streaming multipart upload
// for the recording so the audio is never copied.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "esp_err.h"

namespace http {

struct Header {
    std::string name;
    std::string value;
};

struct Response {
    esp_err_t err = ESP_OK;   // Transport-level failure, ESP_OK when a status arrived
    int status = 0;           // HTTP status, 0 when err is set
    std::string body;         // Response body, capped at 64 KB

    // Returns true for a 2xx status with no transport error.
    bool ok() const { return err == ESP_OK && status >= 200 && status < 300; }

    // One line for logs and the screen: "HTTP 401: ..." or the esp_err name.
    std::string summary() const;
};

// REQUEST SENDER
// Sends one request with an in-memory body and returns the response. Public
// hosts are verified against the certificate bundle; insecure_tls skips
// verification for the Obsidian plugin's self-signed certificate.
Response request(const char *method, const std::string &url, const std::vector<Header> &headers,
                 const std::string &body, bool insecure_tls = false, int timeout_ms = 20000);

// WAV UPLOADER
// Posts a multipart form whose last part is the current recording from
// clip.cpp as a WAV file named note.wav; fields become plain text parts.
// insecure_tls skips verification the same way request() does.
Response post_wav(const std::string &url, const std::vector<Header> &headers,
                  const std::vector<Header> &fields, bool insecure_tls = false,
                  int timeout_ms = 60000);

}  // namespace http
