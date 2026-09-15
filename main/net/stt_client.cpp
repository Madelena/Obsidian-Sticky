// =============================================================================
// STT CLIENT
// =============================================================================
// Multipart WAV upload to an OpenAI-shaped /audio/transcriptions endpoint.
#include "net/stt_client.h"

#include <cstring>

#include "app/settings.h"
#include "audio/clip.h"
#include "esp_log.h"
#include "net/http.h"

namespace stt_client {
namespace {

constexpr const char *kTag = "stt";

// Private LAN hosts use self-signed certificates, public APIs are verified
// against the bundle.
bool is_private_host(const std::string &url)
{
    const size_t scheme = url.find("://");
    const size_t start = scheme == std::string::npos ? 0 : scheme + 3;
    const size_t end = url.find_first_of(":/", start);
    const std::string host = url.substr(start, end == std::string::npos ? end : end - start);
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

// Trims whitespace from both ends.
std::string trim(std::string text)
{
    const char *ws = " \t\r\n";
    const size_t start = text.find_first_not_of(ws);
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = text.find_last_not_of(ws);
    return text.substr(start, end - start + 1);
}

}  // namespace


Result transcribe()
{
    Result result;
    const settings::Values s = settings::get();
    if (s.stt_url.empty() || s.stt_key.empty()) {
        result.error = "STT not configured";
        return result;
    }
    if (clip::sample_count() == 0) {
        result.error = "No audio";
        return result;
    }

    std::vector<http::Header> fields = {
        {"model", s.stt_model},
        {"response_format", "text"},
    };
    if (!s.stt_lang.empty()) {
        fields.push_back({"language", s.stt_lang});
    }
    const std::vector<http::Header> headers = {
        {"Authorization", "Bearer " + s.stt_key},
    };
    const http::Response response =
        http::post_wav(s.stt_url, headers, fields, is_private_host(s.stt_url));
    if (!response.ok()) {
        result.error = response.summary();
        return result;
    }
    result.text = trim(response.body);
    if (result.text.empty()) {
        result.error = "Nothing heard";
        return result;
    }
    result.ok = true;
    ESP_LOGI(kTag, "Transcript: %s", result.text.c_str());
    return result;
}


Result test()
{
    Result result;
    const settings::Values s = settings::get();
    if (s.stt_url.empty() || s.stt_key.empty()) {
        result.error = "STT URL or key missing";
        return result;
    }
    // Every OpenAI-compatible server exposes /models next to /audio/...
    std::string url = s.stt_url;
    const size_t audio = url.find("/audio/");
    if (audio != std::string::npos) {
        url = url.substr(0, audio) + "/models";
    }
    const http::Response response = http::request("GET", url, {{"Authorization", "Bearer " + s.stt_key}},
                                                  "", is_private_host(s.stt_url));
    result.ok = response.ok();
    result.text = result.ok ? "Reachable, key accepted" : "";
    result.error = result.ok ? "" : response.summary();
    return result;
}

}  // namespace stt_client
