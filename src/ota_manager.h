#pragma once

#include <cstdint>

struct OtaInfo {
    bool available;
    int  httpCode;
    char version[16];
    char url[256];
};

// Check GitHub releases for a newer version
OtaInfo otaCheck(const char* repo);

// Download .bin from url and flash via Update library; reboots on success
// Optional progress callback receives percentage (0-100)
bool otaFlash(const char* binUrl, void(*progressCB)(int pct) = nullptr);
