// =============================================================================
// SETTINGS
// =============================================================================
// NVS persistence and JSON exchange for the user settings.
#include "app/settings.h"

#include <mutex>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

namespace settings {
namespace {

constexpr const char *kTag = "settings";
constexpr const char *kNamespace = "sticky";

constexpr const char *kDefaultPrompt =
    "You turn a raw voice transcription into a clean written note. Fix "
    "punctuation, capitalization, and obvious mis-hearings. Remove filler words "
    "(um, uh, like, you know), stutters, and repeated words. When the speaker "
    "corrects themselves (\"I mean\", \"no wait\", \"actually\"), keep only the "
    "corrected version. Keep the speaker's own words, order, and meaning "
    "otherwise: do not summarize, add ideas, or drop content. Reply with only "
    "the cleaned text.";

std::mutex s_mutex;
Values s_values;

// Returns the factory defaults.
Values defaults()
{
    Values v;
    v.stt_url = "https://api.groq.com/openai/v1/audio/transcriptions";
    v.stt_model = "whisper-large-v3-turbo";
    v.llm_kind = "anthropic";
    v.llm_url = "https://api.anthropic.com/v1/messages";
    v.llm_model = "claude-haiku-4-5";
    v.llm_prompt = kDefaultPrompt;
    v.obs_url = "http://192.168.1.2:27123";
    v.obs_mode = "daily";
    v.obs_folder = "Inbox";
    v.obs_line = "- **{time}** {text}";
    v.tz = "EST5EDT,M3.2.0,M11.1.0";
    return v;
}

// Table of string fields: NVS key (15 chars max), JSON name, member, secret.
struct StringField {
    const char *key;
    std::string Values::*member;
    bool secret;
};
constexpr StringField kStrings[] = {
    {"wifi_ssid", &Values::wifi_ssid, false},  {"wifi_pass", &Values::wifi_pass, true},
    {"stt_url", &Values::stt_url, false},      {"stt_model", &Values::stt_model, false},
    {"stt_key", &Values::stt_key, true},       {"stt_lang", &Values::stt_lang, false},
    {"llm_kind", &Values::llm_kind, false},    {"llm_url", &Values::llm_url, false},
    {"llm_model", &Values::llm_model, false},  {"llm_key", &Values::llm_key, true},
    {"llm_prompt", &Values::llm_prompt, false}, {"obs_url", &Values::obs_url, false},
    {"obs_key", &Values::obs_key, true},       {"obs_mode", &Values::obs_mode, false},
    {"obs_folder", &Values::obs_folder, false}, {"obs_line", &Values::obs_line, false},
    {"tz", &Values::tz, false},
};

// Reads one NVS string into dest, leaving dest alone when the key is absent.
void load_string(nvs_handle_t handle, const char *key, std::string &dest)
{
    size_t length = 0;
    if (nvs_get_str(handle, key, nullptr, &length) != ESP_OK || length == 0) {
        return;
    }
    std::string value(length, '\0');
    if (nvs_get_str(handle, key, value.data(), &length) == ESP_OK) {
        value.resize(length - 1);
        dest = value;
    }
}

// Reads one NVS int32 into dest when present.
void load_int(nvs_handle_t handle, const char *key, int &dest)
{
    int32_t value = 0;
    if (nvs_get_i32(handle, key, &value) == ESP_OK) {
        dest = value;
    }
}

}  // namespace


esp_err_t init()
{
    Values loaded = defaults();
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        for (const StringField &field : kStrings) {
            load_string(handle, field.key, loaded.*(field.member));
        }
        int flag = 0;
        load_int(handle, "llm_on", flag);
        loaded.llm_on = flag != 0;
        flag = 1;
        load_int(handle, "beep", flag);
        loaded.beep = flag != 0;
        load_int(handle, "sleep_min", loaded.sleep_min);
        nvs_close(handle);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    s_values = loaded;
    ESP_LOGI(kTag, "Loaded, wifi=%s", loaded.wifi_ssid.empty() ? "(none)" : loaded.wifi_ssid.c_str());
    return ESP_OK;
}


Values get()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_values;
}


esp_err_t save(const Values &values)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    for (const StringField &field : kStrings) {
        if (err == ESP_OK) {
            err = nvs_set_str(handle, field.key, (values.*(field.member)).c_str());
        }
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, "llm_on", values.llm_on ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, "beep", values.beep ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, "sleep_min", values.sleep_min);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_values = values;
    }
    return err;
}


std::string to_json()
{
    const Values v = get();
    cJSON *root = cJSON_CreateObject();
    for (const StringField &field : kStrings) {
        const std::string &value = v.*(field.member);
        if (field.secret) {
            cJSON_AddBoolToObject(root, (std::string(field.key) + "_set").c_str(), !value.empty());
        } else {
            cJSON_AddStringToObject(root, field.key, value.c_str());
        }
    }
    cJSON_AddBoolToObject(root, "llm_on", v.llm_on);
    cJSON_AddBoolToObject(root, "beep", v.beep);
    cJSON_AddNumberToObject(root, "sleep_min", v.sleep_min);
    char *printed = cJSON_PrintUnformatted(root);
    std::string out = printed != nullptr ? printed : "{}";
    cJSON_free(printed);
    cJSON_Delete(root);
    return out;
}


bool apply_json(const char *json, std::string &error)
{
    cJSON *root = cJSON_Parse(json);
    if (root == nullptr) {
        error = "Invalid JSON";
        ESP_LOGW(kTag, "Rejected body: %.80s", json);
        return false;
    }
    Values v = get();
    for (const StringField &field : kStrings) {
        const cJSON *item = cJSON_GetObjectItem(root, field.key);
        if (!cJSON_IsString(item)) {
            continue;
        }
        // A blank secret means "keep what is stored".
        if (field.secret && item->valuestring[0] == '\0') {
            continue;
        }
        v.*(field.member) = item->valuestring;
    }
    const cJSON *llm_on = cJSON_GetObjectItem(root, "llm_on");
    if (cJSON_IsBool(llm_on)) {
        v.llm_on = cJSON_IsTrue(llm_on);
    }
    const cJSON *beep = cJSON_GetObjectItem(root, "beep");
    if (cJSON_IsBool(beep)) {
        v.beep = cJSON_IsTrue(beep);
    }
    const cJSON *sleep_min = cJSON_GetObjectItem(root, "sleep_min");
    if (cJSON_IsNumber(sleep_min)) {
        v.sleep_min = sleep_min->valueint < 0 ? 0 : sleep_min->valueint;
    }
    cJSON_Delete(root);

    if (v.obs_mode != "daily" && v.obs_mode != "note") {
        error = "obs_mode must be daily or note";
        return false;
    }
    if (v.llm_kind != "anthropic" && v.llm_kind != "openai") {
        error = "llm_kind must be anthropic or openai";
        return false;
    }
    const esp_err_t err = save(v);
    if (err != ESP_OK) {
        error = esp_err_to_name(err);
        ESP_LOGW(kTag, "Save failed: %s", error.c_str());
        return false;
    }
    ESP_LOGI(kTag, "Saved, wifi=%s stt_key=%s obs_key=%s", v.wifi_ssid.c_str(),
             v.stt_key.empty() ? "unset" : "set", v.obs_key.empty() ? "unset" : "set");
    return true;
}


bool wifi_configured()
{
    return !get().wifi_ssid.empty();
}

}  // namespace settings
