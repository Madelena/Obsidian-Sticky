// =============================================================================
// WIFI
// =============================================================================
// Station mode for normal use, SoftAP for the setup portal, and SNTP so the
// timestamps in notes are real. Connecting is asynchronous so recording can
// start before the network is up.
#pragma once

#include <cstdint>
#include <string>

#include "esp_err.h"

namespace wifi {

// WIFI INITIALIZER
// Creates netifs, the event loop, and the driver, radio still off.
esp_err_t init();

// STATION CONNECTOR
// Starts connecting to the stored network in the background and keeps
// retrying on disconnect until stop() is called.
esp_err_t connect_async(const std::string &ssid, const std::string &password);

// CONNECTION WAITER
// Blocks until the station has an IP or timeout_ms passes.
bool wait_connected(uint32_t timeout_ms);

// CONNECTION CHECKER
// Returns true while the station holds an IP address.
bool connected();

// ACCESS POINT STARTER
// Brings up an open SoftAP at 192.168.4.1 for the setup portal.
esp_err_t start_ap(const std::string &ssid);

// RADIO STOPPER
// Stops station and access point modes.
void stop();

// ADDRESS GETTERS
// Dotted IP of the active interface (station first, then AP), and the SSID.
std::string ip();
std::string ssid();

// LATENCY SWITCH
// Turns modem power save off while true; uploads run several times faster
// but idle current rises, so the pipeline only sets it during a note.
void set_low_latency(bool on);

// CLOCK SYNCER
// Applies the POSIX TZ string and starts SNTP; safe to call before connect.
void start_sntp(const std::string &tz);

// CLOCK CHECKER
// Returns true once the wall clock has been set by SNTP.
bool time_synced();

}  // namespace wifi
