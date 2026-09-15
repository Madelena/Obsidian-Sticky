// =============================================================================
// OBSIDIAN CLIENT
// =============================================================================
// Local REST API requests for the daily-append and new-note targets.
#include "net/obsidian_client.h"

#include <cctype>
#include <cstdio>
#include <ctime>

#include "app/settings.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "net/http.h"
#include "net/wifi.h"

namespace obsidian_client {
namespace {

constexpr const char *kTag = "obsidian";

// Percent-encodes a vault path, keeping slashes as separators.
std::string encode_path(const std::string &path)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : path) {
        const bool keep = std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
        if (keep) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// Base URL without a trailing slash, and whether TLS verification is skipped.
std::string base_url(const settings::Values &s, bool &insecure)
{
    std::string url = s.obs_url;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    insecure = url.rfind("https://", 0) == 0;
    return url;
}

// Common headers for the plugin.
std::vector<http::Header> headers(const settings::Values &s)
{
    return {{"Authorization", "Bearer " + s.obs_key}, {"Content-Type", "text/markdown"}};
}

// Replaces every occurrence of token in text.
void replace_all(std::string &text, const std::string &token, const std::string &value)
{
    size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) {
        text.replace(pos, token.size(), value);
        pos += value.size();
    }
}

// Formats the local time with strftime, or a fallback when unsynced.
std::string now(const char *format, const char *fallback)
{
    if (!wifi::time_synced()) {
        return fallback;
    }
    const time_t t = time(nullptr);
    struct tm local = {};
    localtime_r(&t, &local);
    char buffer[40];
    std::strftime(buffer, sizeof(buffer), format, &local);
    return buffer;
}

// Appends the rendered template line to today's daily note.
Result save_daily(const settings::Values &s, const std::string &url, bool insecure,
                  const std::string &text)
{
    std::string single_line = text;
    replace_all(single_line, "\r", "");
    replace_all(single_line, "\n", " ");
    std::string line = s.obs_line.empty() ? "- {text}" : s.obs_line;
    replace_all(line, "{time}", now("%H:%M", "--:--"));
    replace_all(line, "{text}", single_line);
    line += "\n";

    Result result;
    const http::Response response =
        http::request("POST", url + "/periodic/daily/", headers(s), line, insecure);
    if (response.ok()) {
        result.ok = true;
        result.target = "Daily note";
    } else if (response.status == 404) {
        result.error = "Periodic Notes plugin missing";
    } else {
        result.error = response.summary();
    }
    return result;
}

// Creates a new note with frontmatter under the configured folder.
Result save_note(const settings::Values &s, const std::string &url, bool insecure,
                 const std::string &text)
{
    char fallback[40];
    std::snprintf(fallback, sizeof(fallback), "Voice note %llu",
                  static_cast<unsigned long long>(esp_timer_get_time() / 1000000));
    std::string name = now("%Y-%m-%d %H%M Voice note", fallback);
    std::string path = s.obs_folder;
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    path = (path.empty() ? "" : path + "/") + name + ".md";

    std::string body = "---\ncreated: " + now("%Y-%m-%dT%H:%M:%S", "unknown") +
                       "\nsource: reterminal-sticky\n---\n\n" + text + "\n";

    Result result;
    const http::Response response =
        http::request("PUT", url + "/vault/" + encode_path(path), headers(s), body, insecure);
    if (response.ok()) {
        result.ok = true;
        result.target = path;
    } else {
        result.error = response.summary();
    }
    return result;
}

}  // namespace


Result save(const std::string &text)
{
    Result result;
    const settings::Values s = settings::get();
    if (s.obs_url.empty() || s.obs_key.empty()) {
        result.error = "Obsidian not configured";
        return result;
    }
    bool insecure = false;
    const std::string url = base_url(s, insecure);
    result = s.obs_mode == "note" ? save_note(s, url, insecure, text)
                                  : save_daily(s, url, insecure, text);
    ESP_LOGI(kTag, "Save %s: %s", result.ok ? "ok" : "failed",
             result.ok ? result.target.c_str() : result.error.c_str());
    return result;
}


Result test()
{
    Result result;
    const settings::Values s = settings::get();
    if (s.obs_url.empty() || s.obs_key.empty()) {
        result.error = "Obsidian URL or key missing";
        return result;
    }
    bool insecure = false;
    const std::string url = base_url(s, insecure);
    const http::Response status = http::request("GET", url + "/vault/", headers(s), "", insecure);
    if (!status.ok()) {
        result.error = status.status == 401 ? "Key rejected (HTTP 401)" : status.summary();
        return result;
    }
    if (s.obs_mode == "daily") {
        const http::Response daily = http::request("GET", url + "/periodic/daily/", headers(s), "", insecure);
        if (daily.status == 404) {
            result.error = "Vault reachable, but the Periodic Notes companion plugin is not installed";
            return result;
        }
    }
    result.ok = true;
    result.target = s.obs_mode == "daily" ? "Vault and daily note reachable" : "Vault reachable";
    return result;
}

}  // namespace obsidian_client
