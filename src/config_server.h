#pragma once

#include <cstdint>

// Lightweight HTTP server that runs after the device connects to WiFi so
// the user can edit app config (watchlist today, alerts in v5.1) from a
// browser on the same network — no captive portal tear-down required.
//
// Lifecycle: configServerStart() once WiFi is up; configServerStop()
// before OTA or shutdown.

void configServerStart();
void configServerStop();
bool configServerRunning();
const char* configServerIp();    // Best-effort: device STA IP as C string
