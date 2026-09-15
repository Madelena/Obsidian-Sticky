// =============================================================================
// PORTAL
// =============================================================================
// esp_http_server routes for the embedded settings page and its JSON API.
#include "portal/portal.h"

#include <string>

#include "app/settings.h"
#include "cJSON.h"
// dns_server.h names esp_ip4_addr_t without including what defines it.
#include "esp_netif.h"
#include "dns_server.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/llm_client.h"
#include "net/obsidian_client.h"
#include "net/stt_client.h"
#include "ui/screen.h"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

namespace portal {
namespace {

constexpr const char *kTag = "portal";
constexpr size_t kMaxBody = 8192;

httpd_handle_t s_server = nullptr;
dns_server_handle_t s_dns = nullptr;
bool s_captive = false;

// Sends a small JSON object {ok, message}.
esp_err_t send_result(httpd_req_t *req, bool ok, const std::string &message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddStringToObject(root, "message", message.c_str());
    char *printed = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_sendstr(req, printed != nullptr ? printed : "{}");
    cJSON_free(printed);
    cJSON_Delete(root);
    return err;
}

// Reads the whole request body, or returns false when it is too large.
bool read_body(httpd_req_t *req, std::string &body)
{
    if (req->content_len == 0 || req->content_len > kMaxBody) {
        ESP_LOGW(kTag, "Rejected body of %u bytes", static_cast<unsigned>(req->content_len));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body missing or too large");
        return false;
    }
    body.resize(req->content_len);
    size_t received = 0;
    while (received < body.size()) {
        const int got = httpd_req_recv(req, body.data() + received, body.size() - received);
        if (got <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body read failed");
            return false;
        }
        received += got;
    }
    return true;
}

esp_err_t handle_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

// Reports whether the page is being served from the setup hotspot, where
// the test buttons cannot reach the internet.
esp_err_t handle_get_mode(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, s_captive ? "{\"captive\":true}" : "{\"captive\":false}");
}

esp_err_t handle_get_settings(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, settings::to_json().c_str());
}

esp_err_t handle_post_settings(httpd_req_t *req)
{
    std::string body;
    if (!read_body(req, body)) {
        return ESP_FAIL;
    }
    std::string error;
    const bool ok = settings::apply_json(body.c_str(), error);
    ESP_LOGI(kTag, "POST /api/settings: %u bytes, %s", static_cast<unsigned>(body.size()),
             ok ? "saved" : error.c_str());
    return send_result(req, ok, ok ? "Saved" : error);
}

esp_err_t handle_test(httpd_req_t *req)
{
    const std::string uri = req->uri;
    bool ok = false;
    std::string message;
    if (uri.find("/stt") != std::string::npos) {
        const stt_client::Result r = stt_client::test();
        ok = r.ok;
        message = ok ? r.text : r.error;
    } else if (uri.find("/llm") != std::string::npos) {
        const llm_client::Result r = llm_client::test();
        ok = r.ok;
        message = ok ? r.text : r.error;
    } else {
        const obsidian_client::Result r = obsidian_client::test();
        ok = r.ok;
        message = ok ? r.target : r.error;
    }
    return send_result(req, ok, message);
}

// Shows posted UTF-8 text as the note, for checking fonts and layout from a
// computer without recording anything.
esp_err_t handle_show(httpd_req_t *req)
{
    std::string body;
    if (!read_body(req, body)) {
        return ESP_FAIL;
    }
    screen::set_note(body);
    screen::set_caption("Preview from the settings page");
    screen::show("Preview", -1, true);
    return send_result(req, true, "Shown");
}

esp_err_t handle_reboot(httpd_req_t *req)
{
    send_result(req, true, "Restarting");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// Captive-portal catch-all: every unknown path redirects to the page.
esp_err_t handle_not_found(httpd_req_t *req, httpd_err_code_t)
{
    if (!s_captive) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_sendstr(req, "Redirecting to the Sticky setup page");
    return ESP_OK;
}

}  // namespace


esp_err_t start(bool captive)
{
    if (s_server != nullptr) {
        return ESP_OK;
    }
    s_captive = captive;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Test routes run TLS clients inside the handler.
    config.stack_size = 16384;
    config.max_uri_handlers = 10;
    config.lru_purge_enable = true;
    // Phones on the captive hotspot open many probe connections; drop idle
    // ones quickly so the page's own requests always find a free socket.
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.uri_match_fn = httpd_uri_match_wildcard;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), kTag, "httpd");

    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, handle_index, nullptr},
        {"/api/mode", HTTP_GET, handle_get_mode, nullptr},
        {"/api/settings", HTTP_GET, handle_get_settings, nullptr},
        {"/api/settings", HTTP_POST, handle_post_settings, nullptr},
        {"/api/test/*", HTTP_POST, handle_test, nullptr},
        {"/api/reboot", HTTP_POST, handle_reboot, nullptr},
        {"/api/show", HTTP_POST, handle_show, nullptr},
    };
    for (const httpd_uri_t &route : routes) {
        httpd_register_uri_handler(s_server, &route);
    }
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, handle_not_found);

    if (captive) {
        // Spelled out rather than DNS_SERVER_CONFIG_SINGLE, whose expansion
        // leaves .ip uninitialized and trips -Wmissing-field-initializers.
        dns_server_config_t dns = {};
        dns.num_of_entries = 1;
        dns.item[0].name = "*";
        dns.item[0].if_key = "WIFI_AP_DEF";
        s_dns = start_dns_server(&dns);
    }
    ESP_LOGI(kTag, "Portal up (%s)", captive ? "captive" : "station");
    return ESP_OK;
}


void stop()
{
    if (s_dns != nullptr) {
        stop_dns_server(s_dns);
        s_dns = nullptr;
    }
    if (s_server != nullptr) {
        httpd_stop(s_server);
        s_server = nullptr;
    }
}

}  // namespace portal
