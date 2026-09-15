// =============================================================================
// PORTAL
// =============================================================================
// The settings web page served by the device: on the SoftAP as a captive
// portal during setup, and on the LAN address afterwards. Talks to
// settings.cpp and the three API clients' test() functions.
#pragma once

#include "esp_err.h"

namespace portal {

// PORTAL STARTER
// Starts the HTTP server; captive also starts the DNS catch-all and 404
// redirects so phones open the page automatically.
esp_err_t start(bool captive);

// PORTAL STOPPER
// Stops the HTTP and DNS servers.
void stop();

}  // namespace portal
