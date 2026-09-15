// =============================================================================
// WIFI
// =============================================================================
// ESP-IDF Wi-Fi station, SoftAP, and SNTP wiring.
#include "net/wifi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace wifi {
namespace {

constexpr const char *kTag = "wifi";
constexpr EventBits_t kConnectedBit = BIT0;

esp_netif_t *s_sta_netif = nullptr;
esp_netif_t *s_ap_netif = nullptr;
EventGroupHandle_t s_events = nullptr;
bool s_started = false;
bool s_want_sta = false;
bool s_sntp_started = false;
std::string s_ssid;
esp_timer_handle_t s_dhcp_watchdog = nullptr;
// Some mesh nodes accept the association but never answer DHCP; without a
// disconnect the driver would sit there forever, so this forces a retry.
constexpr uint64_t kDhcpWatchdogUs = 12ULL * 1000 * 1000;

// Drops an association that never produced an IP so the driver reconnects.
void on_dhcp_watchdog(void *)
{
    if (s_want_sta && (xEventGroupGetBits(s_events) & kConnectedBit) == 0) {
        ESP_LOGW(kTag, "Associated but no IP after 12 s, reconnecting");
        esp_wifi_disconnect();
    }
}

// Reconnects on drop and records the IP when it arrives.
void on_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START && s_want_sta) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        esp_timer_stop(s_dhcp_watchdog);
        esp_timer_start_once(s_dhcp_watchdog, kDhcpWatchdogUs);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_timer_stop(s_dhcp_watchdog);
        xEventGroupClearBits(s_events, kConnectedBit);
        if (s_want_sta) {
            ESP_LOGW(kTag, "Disconnected, retrying");
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        esp_timer_stop(s_dhcp_watchdog);
        const auto *event = static_cast<ip_event_got_ip_t *>(data);
        ESP_LOGI(kTag, "Got IP " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_events, kConnectedBit);
    }
}

// Formats a netif's IPv4 address, or an empty string.
std::string netif_ip(esp_netif_t *netif)
{
    esp_netif_ip_info_t info = {};
    if (netif == nullptr || esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        return "";
    }
    char text[16];
    std::snprintf(text, sizeof(text), IPSTR, IP2STR(&info.ip));
    return text;
}

}  // namespace


esp_err_t init()
{
    if (s_events != nullptr) {
        return ESP_OK;
    }
    s_events = xEventGroupCreate();
    esp_timer_create_args_t watchdog = {};
    watchdog.callback = on_dhcp_watchdog;
    watchdog.name = "dhcp_watchdog";
    ESP_RETURN_ON_ERROR(esp_timer_create(&watchdog, &s_dhcp_watchdog), kTag, "watchdog");
    ESP_RETURN_ON_ERROR(esp_netif_init(), kTag, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), kTag, "event loop");
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), kTag, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr),
                        kTag, "wifi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr),
                        kTag, "ip events");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), kTag, "storage");
    return ESP_OK;
}


esp_err_t connect_async(const std::string &ssid, const std::string &password)
{
    wifi_config_t config = {};
    std::strncpy(reinterpret_cast<char *>(config.sta.ssid), ssid.c_str(), sizeof(config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char *>(config.sta.password), password.c_str(),
                 sizeof(config.sta.password) - 1);
    config.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    config.sta.pmf_cfg.capable = true;
    // Scan every channel and prefer the strongest node of a mesh network.
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    s_ssid = ssid;
    s_want_sta = true;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "mode sta");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), kTag, "sta config");
    if (!s_started) {
        ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "start");
        s_started = true;
    } else {
        esp_wifi_connect();
    }
    return ESP_OK;
}


bool wait_connected(uint32_t timeout_ms)
{
    if (s_events == nullptr) {
        return false;
    }
    const EventBits_t bits = xEventGroupWaitBits(s_events, kConnectedBit, pdFALSE, pdTRUE,
                                                 pdMS_TO_TICKS(timeout_ms));
    return (bits & kConnectedBit) != 0;
}


bool connected()
{
    return s_events != nullptr && (xEventGroupGetBits(s_events) & kConnectedBit) != 0;
}


esp_err_t start_ap(const std::string &ssid)
{
    s_want_sta = false;
    wifi_config_t config = {};
    std::strncpy(reinterpret_cast<char *>(config.ap.ssid), ssid.c_str(), sizeof(config.ap.ssid) - 1);
    config.ap.ssid_len = ssid.size();
    config.ap.channel = 6;
    config.ap.max_connection = 4;
    config.ap.authmode = WIFI_AUTH_OPEN;
    s_ssid = ssid;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), kTag, "mode ap");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), kTag, "ap config");
    if (!s_started) {
        ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "start");
        s_started = true;
    }
    ESP_LOGI(kTag, "SoftAP %s up at %s", ssid.c_str(), netif_ip(s_ap_netif).c_str());
    return ESP_OK;
}


void stop()
{
    s_want_sta = false;
    if (s_started) {
        esp_wifi_stop();
        s_started = false;
    }
    if (s_events != nullptr) {
        xEventGroupClearBits(s_events, kConnectedBit);
    }
}


std::string ip()
{
    std::string address = connected() ? netif_ip(s_sta_netif) : "";
    if (address.empty()) {
        address = netif_ip(s_ap_netif);
    }
    return address;
}


std::string ssid()
{
    return s_ssid;
}


void set_low_latency(bool on)
{
    esp_wifi_set_ps(on ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
}


void start_sntp(const std::string &tz)
{
    setenv("TZ", tz.c_str(), 1);
    tzset();
    if (s_sntp_started) {
        return;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.start = true;
    if (esp_netif_sntp_init(&config) == ESP_OK) {
        s_sntp_started = true;
    }
}


bool time_synced()
{
    // Anything before 2024 means the clock is still at the epoch default.
    return time(nullptr) > 1704067200;
}

}  // namespace wifi
