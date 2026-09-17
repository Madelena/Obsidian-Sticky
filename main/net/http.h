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

struct WarmTarget {
    std::string url;
    bool insecure_tls = false;
};

// PRIVATE HOST CHECKER
// Returns true for a LAN or link-local host, which is where a self-signed
// certificate is expected; public hosts are verified against the bundle.
bool is_private_host(const std::string &url);

// CONNECTION WARMER
// Opens connections to the given hosts in the background and holds them, so a
// later request to one of those hosts skips DNS, TCP and the TLS handshake.
// That handshake measures about 1.8 seconds on this chip, so warming is
// started while the user is still speaking rather than after they stop.
void warm_async(const std::vector<WarmTarget> &targets);

// WARMER WAITER
// Blocks until background warming has finished or timeout_ms passes, so a
// request never races the handshake it is trying to skip.
void wait_warm(uint32_t timeout_ms);

// WARM CONNECTION DROPPER
// Closes and forgets every held connection. Safe when none is held.
void drop_warm();

// WAV UPLOADER
// Posts a multipart form whose last part is count samples of the clip.cpp ring
// from first_sample, as a WAV file named note.wav; fields become plain text
// parts. insecure_tls skips verification the same way request() does. The
// range must stay resident for the whole call, because the body is replayed
// on a redirect or a stale socket.
Response post_wav(const std::string &url, const std::vector<Header> &headers,
                  const std::vector<Header> &fields, uint32_t first_sample, size_t count,
                  bool insecure_tls = false, int timeout_ms = 60000);

}  // namespace http
