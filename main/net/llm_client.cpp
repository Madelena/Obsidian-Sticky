// =============================================================================
// LLM CLIENT
// =============================================================================
// Anthropic Messages and OpenAI chat-completions request shapes.
#include "net/llm_client.h"

#include "app/settings.h"
#include "cJSON.h"
#include "esp_log.h"
#include "net/http.h"

namespace llm_client {
namespace {

constexpr const char *kTag = "llm";

// Serializes a cJSON tree and frees it.
std::string dump(cJSON *root)
{
    char *printed = cJSON_PrintUnformatted(root);
    std::string out = printed != nullptr ? printed : "";
    cJSON_free(printed);
    cJSON_Delete(root);
    return out;
}

// Builds the Anthropic Messages request body.
std::string anthropic_body(const settings::Values &s, const std::string &user_text)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", s.llm_model.c_str());
    cJSON_AddNumberToObject(root, "max_tokens", 1024);
    cJSON_AddStringToObject(root, "system", s.llm_prompt.c_str());
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", "user");
    cJSON_AddStringToObject(message, "content", user_text.c_str());
    cJSON_AddItemToArray(messages, message);
    return dump(root);
}

// Builds the OpenAI chat-completions request body.
std::string openai_body(const settings::Values &s, const std::string &user_text)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", s.llm_model.c_str());
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *system = cJSON_CreateObject();
    cJSON_AddStringToObject(system, "role", "system");
    cJSON_AddStringToObject(system, "content", s.llm_prompt.c_str());
    cJSON_AddItemToArray(messages, system);
    cJSON *user = cJSON_CreateObject();
    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", user_text.c_str());
    cJSON_AddItemToArray(messages, user);
    return dump(root);
}

// Pulls the reply text out of either provider's response JSON.
bool extract_text(bool anthropic, const std::string &json, std::string &text)
{
    cJSON *root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        return false;
    }
    const cJSON *item = nullptr;
    if (anthropic) {
        const cJSON *content = cJSON_GetObjectItem(root, "content");
        const cJSON *first = cJSON_GetArrayItem(content, 0);
        item = cJSON_GetObjectItem(first, "text");
    } else {
        const cJSON *choices = cJSON_GetObjectItem(root, "choices");
        const cJSON *first = cJSON_GetArrayItem(choices, 0);
        const cJSON *message = cJSON_GetObjectItem(first, "message");
        item = cJSON_GetObjectItem(message, "content");
    }
    const bool found = cJSON_IsString(item);
    if (found) {
        text = item->valuestring;
    }
    cJSON_Delete(root);
    return found;
}

// Sends user_text through the configured provider.
Result send(const std::string &user_text)
{
    Result result;
    const settings::Values s = settings::get();
    if (s.llm_url.empty() || s.llm_key.empty()) {
        result.error = "LLM not configured";
        return result;
    }
    const bool anthropic = s.llm_kind == "anthropic";
    std::vector<http::Header> headers = {{"Content-Type", "application/json"}};
    if (anthropic) {
        headers.push_back({"x-api-key", s.llm_key});
        headers.push_back({"anthropic-version", "2023-06-01"});
    } else {
        headers.push_back({"Authorization", "Bearer " + s.llm_key});
    }
    const std::string body = anthropic ? anthropic_body(s, user_text) : openai_body(s, user_text);
    const http::Response response = http::request("POST", s.llm_url, headers, body, false, 40000);
    if (!response.ok()) {
        result.error = response.summary();
        return result;
    }
    if (!extract_text(anthropic, response.body, result.text)) {
        result.error = "Unexpected reply shape";
        return result;
    }
    // Strip the trailing newline most models append.
    while (!result.text.empty() && (result.text.back() == '\n' || result.text.back() == ' ')) {
        result.text.pop_back();
    }
    result.ok = !result.text.empty();
    if (!result.ok) {
        result.error = "Empty reply";
    }
    return result;
}

}  // namespace


Result clean(const std::string &transcript)
{
    Result result = send(transcript);
    if (result.ok) {
        ESP_LOGI(kTag, "Cleaned: %s", result.text.c_str());
    }
    return result;
}


Result test()
{
    Result result = send("Reply with the single word OK.");
    if (result.ok) {
        result.text = "Model replied: " + result.text.substr(0, 40);
    }
    return result;
}

}  // namespace llm_client
