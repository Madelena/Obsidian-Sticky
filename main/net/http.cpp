// =============================================================================
// HTTP
// =============================================================================
// esp_http_client requests with captured bodies, redirect following, and the
// streamed WAV upload.
#include "net/http.h"

#include <algorithm>
#include <cstring>
#include <functional>

#include "audio/clip.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

namespace http {
namespace {

constexpr const char *kTag = "http";
constexpr size_t kMaxBody = 64 * 1024;
constexpr size_t kUploadChunk = 16 * 1024;
constexpr int kMaxRedirects = 3;
constexpr const char *kBoundary = "----ObsidianStickyBoundary7f3a9c";

// Reads the whole response body into out, truncating at kMaxBody.
void read_body(esp_http_client_handle_t client, std::string &out)
{
    char buffer[2048];
    while (true) {
        const int got = esp_http_client_read(client, buffer, sizeof(buffer));
        if (got <= 0) {
            break;
        }
        if (out.size() < kMaxBody) {
            out.append(buffer, std::min(static_cast<size_t>(got), kMaxBody - out.size()));
        }
    }
}

// Builds a client for url with the TLS policy and headers applied.
esp_http_client_handle_t make_client(const std::string &url, const char *method,
                                     const std::vector<Header> &headers, bool insecure_tls,
                                     int timeout_ms)
{
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = timeout_ms;
    config.buffer_size = 4096;
    config.buffer_size_tx = 4096;
    if (insecure_tls) {
        config.skip_cert_common_name_check = true;
    } else {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return nullptr;
    }
    esp_http_client_set_method(client, std::strcmp(method, "PUT") == 0    ? HTTP_METHOD_PUT
                                       : std::strcmp(method, "POST") == 0 ? HTTP_METHOD_POST
                                       : std::strcmp(method, "PATCH") == 0 ? HTTP_METHOD_PATCH
                                                                            : HTTP_METHOD_GET);
    for (const Header &header : headers) {
        esp_http_client_set_header(client, header.name.c_str(), header.value.c_str());
    }
    return client;
}

// Writes all of data, returning false on a short write.
bool write_all(esp_http_client_handle_t client, const char *data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        const int wrote = esp_http_client_write(client, data + sent, length - sent);
        if (wrote <= 0) {
            return false;
        }
        sent += wrote;
    }
    return true;
}

// Returns true for a status the client should follow to its Location.
bool is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// Sends the request once per hop and reads the reply, following redirects.
// The open/write/fetch flow does not follow them by itself: 307 and 308
// replay the body (the Periodic Notes plugin answers 307), older codes turn
// into a GET.
Response exchange(esp_http_client_handle_t client, size_t body_length,
                  const std::function<bool()> &write_body)
{
    Response response;
    for (int hop = 0; hop <= kMaxRedirects; ++hop) {
        response.err = esp_http_client_open(client, body_length);
        if (response.err == ESP_OK && body_length > 0 && !write_body()) {
            response.err = ESP_FAIL;
        }
        if (response.err == ESP_OK) {
            if (esp_http_client_fetch_headers(client) < 0) {
                response.err = ESP_FAIL;
            } else {
                response.status = esp_http_client_get_status_code(client);
                response.body.clear();
                read_body(client, response.body);
            }
        }
        esp_http_client_close(client);
        if (response.err != ESP_OK || !is_redirect(response.status) || hop == kMaxRedirects) {
            break;
        }
        if (esp_http_client_set_redirection(client) != ESP_OK) {
            break;
        }
        if (response.status != 307 && response.status != 308) {
            esp_http_client_set_method(client, HTTP_METHOD_GET);
            body_length = 0;
        }
    }
    return response;
}

}  // namespace


std::string Response::summary() const
{
    if (err != ESP_OK) {
        return esp_err_to_name(err);
    }
    std::string text = "HTTP " + std::to_string(status);
    if (!ok() && !body.empty()) {
        text += ": " + body.substr(0, 120);
    }
    return text;
}


Response request(const char *method, const std::string &url, const std::vector<Header> &headers,
                 const std::string &body, bool insecure_tls, int timeout_ms)
{
    Response response;
    esp_http_client_handle_t client = make_client(url, method, headers, insecure_tls, timeout_ms);
    if (client == nullptr) {
        response.err = ESP_ERR_NO_MEM;
        return response;
    }
    response = exchange(client, body.size(),
                        [&]() { return write_all(client, body.data(), body.size()); });
    esp_http_client_cleanup(client);
    ESP_LOGI(kTag, "%s %s -> %s", method, url.c_str(), response.summary().c_str());
    return response;
}


Response post_wav(const std::string &url, const std::vector<Header> &headers,
                  const std::vector<Header> &fields, bool insecure_tls, int timeout_ms)
{
    std::string preamble;
    for (const Header &field : fields) {
        preamble += "--";
        preamble += kBoundary;
        preamble += "\r\nContent-Disposition: form-data; name=\"" + field.name + "\"\r\n\r\n";
        preamble += field.value + "\r\n";
    }
    preamble += "--";
    preamble += kBoundary;
    preamble += "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"note.wav\"\r\n"
                "Content-Type: audio/wav\r\n\r\n";
    const std::string epilogue = std::string("\r\n--") + kBoundary + "--\r\n";
    const size_t total = preamble.size() + clip::wav_size() + epilogue.size();

    std::vector<Header> all = headers;
    all.push_back({"Content-Type", std::string("multipart/form-data; boundary=") + kBoundary});

    Response response;
    esp_http_client_handle_t client = make_client(url, "POST", all, insecure_tls, timeout_ms);
    if (client == nullptr) {
        response.err = ESP_ERR_NO_MEM;
        return response;
    }

    // Streams header, WAV header, PCM in chunks, and trailer from the clip.
    auto stream_form = [&]() {
        uint8_t wav_header[clip::kWavHeaderSize];
        clip::write_wav_header(wav_header);
        const char *pcm = reinterpret_cast<const char *>(clip::samples());
        const size_t pcm_bytes = clip::sample_count() * sizeof(int16_t);
        bool sent = write_all(client, preamble.data(), preamble.size()) &&
                    write_all(client, reinterpret_cast<const char *>(wav_header), sizeof(wav_header));
        for (size_t offset = 0; sent && offset < pcm_bytes; offset += kUploadChunk) {
            sent = write_all(client, pcm + offset, std::min(kUploadChunk, pcm_bytes - offset));
        }
        return sent && write_all(client, epilogue.data(), epilogue.size());
    };
    response = exchange(client, total, stream_form);
    esp_http_client_cleanup(client);
    ESP_LOGI(kTag, "POST wav %u bytes to %s -> %s", static_cast<unsigned>(total), url.c_str(),
             response.summary().c_str());
    return response;
}

}  // namespace http
