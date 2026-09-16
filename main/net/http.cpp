// =============================================================================
// HTTP
// =============================================================================
// esp_http_client requests with captured bodies, redirect following, and the
// streamed WAV upload.
#include "net/http.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>

#include "audio/clip.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace http {
namespace {

constexpr const char *kTag = "http";
constexpr size_t kMaxBody = 64 * 1024;
constexpr size_t kUploadChunk = 16 * 1024;
constexpr int kMaxRedirects = 3;
constexpr const char *kBoundary = "----ObsidianStickyBoundary7f3a9c";

// Connections opened during recording and handed to the requests that follow.
// esp_http_client_set_url only drops the socket when the host changes, so a
// warm-up may use a different path on the same host. Two slots cover the
// transcription host and the cleanup host.
constexpr size_t kWarmSlots = 2;
struct WarmSlot {
    esp_http_client_handle_t client = nullptr;
    std::string host;
};
WarmSlot s_warm[kWarmSlots];
std::mutex s_warm_mutex;
std::atomic<bool> s_warming{false};

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

// Returns the host part of a URL, without scheme, port or path.
std::string host_of(const std::string &url)
{
    const size_t scheme = url.find("://");
    const size_t start = scheme == std::string::npos ? 0 : scheme + 3;
    const size_t end = url.find_first_of(":/", start);
    return url.substr(start, end == std::string::npos ? end : end - start);
}


// Milliseconds since boot, for the phase timings in exchange().
unsigned now_ms()
{
    return static_cast<unsigned>(esp_timer_get_time() / 1000);
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
        const unsigned started = now_ms();
        response.err = esp_http_client_open(client, body_length);
        const unsigned connected = now_ms();
        if (response.err == ESP_OK && body_length > 0 && !write_body()) {
            response.err = ESP_FAIL;
        }
        const unsigned uploaded = now_ms();
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

        // Splits one round trip so a slow note can be blamed on the right
        // thing: connect is DNS, TCP and the TLS handshake, upload is the
        // request body going out, and answer is the wait for the reply.
        // Upload throughput is the number to watch when choosing a codec.
        const unsigned upload_ms = uploaded - connected;
        ESP_LOGI(kTag, "hop %d: connect %u ms, upload %u ms (%u bytes, %u kB/s), answer %u ms",
                 hop, connected - started, upload_ms, static_cast<unsigned>(body_length),
                 upload_ms > 0 ? static_cast<unsigned>(body_length / upload_ms) : 0u,
                 now_ms() - uploaded);

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


// Takes a held connection for this URL, or null when none matches. The socket
// survives the change of path because only a host change closes it.
esp_http_client_handle_t claim_warm(const std::string &url)
{
    const std::string host = host_of(url);
    std::lock_guard<std::mutex> lock(s_warm_mutex);
    for (WarmSlot &slot : s_warm) {
        if (slot.client != nullptr && slot.host == host) {
            esp_http_client_handle_t client = slot.client;
            slot.client = nullptr;
            slot.host.clear();
            esp_http_client_set_url(client, url.c_str());
            return client;
        }
    }
    return nullptr;
}

// Reuses a warmed connection when one is held, otherwise builds a new client.
esp_http_client_handle_t take_or_make(const std::string &url, const char *method,
                                      const std::vector<Header> &headers, bool insecure_tls,
                                      int timeout_ms, bool &reused)
{
    esp_http_client_handle_t client = claim_warm(url);
    reused = client != nullptr;
    if (!reused) {
        return make_client(url, method, headers, insecure_tls, timeout_ms);
    }
    esp_http_client_set_method(client, std::strcmp(method, "PUT") == 0    ? HTTP_METHOD_PUT
                                       : std::strcmp(method, "POST") == 0 ? HTTP_METHOD_POST
                                       : std::strcmp(method, "PATCH") == 0 ? HTTP_METHOD_PATCH
                                                                           : HTTP_METHOD_GET);
    esp_http_client_set_timeout_ms(client, timeout_ms);
    for (const Header &header : headers) {
        esp_http_client_set_header(client, header.name.c_str(), header.value.c_str());
    }
    return client;
}

// Opens one connection and leaves it open, which is the whole point.
void warm_one(const WarmTarget &target)
{
    esp_http_client_handle_t client = make_client(target.url, "GET", {}, target.insecure_tls, 15000);
    if (client == nullptr) {
        return;
    }
    const unsigned started = now_ms();
    bool held = false;
    if (esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0) {
        std::string discard;
        read_body(client, discard);
        std::lock_guard<std::mutex> lock(s_warm_mutex);
        for (WarmSlot &slot : s_warm) {
            if (slot.client == nullptr) {
                slot.client = client;
                slot.host = host_of(target.url);
                held = true;
                break;
            }
        }
    }
    ESP_LOGI(kTag, "warm %s: %u ms, %s", host_of(target.url).c_str(), now_ms() - started,
             held ? "holding" : "failed");
    if (!held) {
        esp_http_client_cleanup(client);
    }
}

// Warms every target in turn on its own task, so recording is not blocked.
void warm_task(void *arg)
{
    std::unique_ptr<std::vector<WarmTarget>> targets(static_cast<std::vector<WarmTarget> *>(arg));
    for (const WarmTarget &target : *targets) {
        warm_one(target);
    }
    s_warming.store(false);
    vTaskDelete(nullptr);
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
    bool reused = false;
    esp_http_client_handle_t client =
        take_or_make(url, method, headers, insecure_tls, timeout_ms, reused);
    if (client == nullptr) {
        response.err = ESP_ERR_NO_MEM;
        return response;
    }
    response = exchange(client, body.size(),
                        [&]() { return write_all(client, body.data(), body.size()); });
    // A held connection the server had already dropped fails at the transport,
    // so pay the handshake once rather than failing the note.
    if (reused && response.err != ESP_OK) {
        esp_http_client_cleanup(client);
        client = make_client(url, method, headers, insecure_tls, timeout_ms);
        if (client != nullptr) {
            ESP_LOGW(kTag, "warm connection was stale, reconnecting");
            response = exchange(client, body.size(),
                                [&]() { return write_all(client, body.data(), body.size()); });
        }
    }
    if (client != nullptr) {
        esp_http_client_cleanup(client);
    }
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
    bool reused = false;
    esp_http_client_handle_t client = take_or_make(url, "POST", all, insecure_tls, timeout_ms, reused);
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
    if (reused && response.err != ESP_OK) {
        esp_http_client_cleanup(client);
        client = make_client(url, "POST", all, insecure_tls, timeout_ms);
        if (client != nullptr) {
            ESP_LOGW(kTag, "warm connection was stale, reconnecting");
            response = exchange(client, total, stream_form);
        }
    }
    if (client != nullptr) {
        esp_http_client_cleanup(client);
    }
    ESP_LOGI(kTag, "POST wav %u bytes to %s -> %s", static_cast<unsigned>(total), url.c_str(),
             response.summary().c_str());
    return response;
}



bool is_private_host(const std::string &url)
{
    const std::string host = host_of(url);
    if (host == "localhost" || host.rfind("192.168.", 0) == 0 || host.rfind("10.", 0) == 0 ||
        host.rfind("172.", 0) == 0) {
        return true;
    }
    const auto ends_with = [&host](const char *suffix) {
        const size_t n = std::strlen(suffix);
        return host.size() >= n && host.compare(host.size() - n, n, suffix) == 0;
    };
    return ends_with(".local") || ends_with(".lan");
}


void warm_async(const std::vector<WarmTarget> &targets)
{
    if (targets.empty() || s_warming.exchange(true)) {
        return;
    }
    auto *copy = new (std::nothrow) std::vector<WarmTarget>(targets);
    if (copy == nullptr) {
        s_warming.store(false);
        return;
    }
    // 8 KB covers a TLS handshake against the certificate bundle.
    if (xTaskCreate(warm_task, "warm", 8192, copy, 4, nullptr) != pdPASS) {
        delete copy;
        s_warming.store(false);
    }
}


void wait_warm(uint32_t timeout_ms)
{
    uint32_t waited = 0;
    while (s_warming.load() && waited < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
    }
}


void drop_warm()
{
    std::lock_guard<std::mutex> lock(s_warm_mutex);
    for (WarmSlot &slot : s_warm) {
        if (slot.client != nullptr) {
            esp_http_client_close(slot.client);
            esp_http_client_cleanup(slot.client);
            slot.client = nullptr;
            slot.host.clear();
        }
    }
}

}  // namespace http
