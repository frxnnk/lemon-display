#pragma once

#include <cstdint>

struct OtaInfo {
    bool available;
    int  httpCode;
    char version[16];
    char url[256];
    char md5[33];     // MD5 hash from release body (empty if none found)
};

// Check GitHub releases for a newer version
OtaInfo otaCheck(const char* repo);

// Free TLS resources from otaCheck (call before otaFlash to maximize heap)
void otaFreeCheck();

// Download .bin from url and flash via Update library; reboots on success
// Optional progress callback receives percentage (0-100)
bool otaFlash(const char* binUrl, void(*progressCB)(int pct) = nullptr, const char* md5 = nullptr);
