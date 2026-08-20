#pragma once

#include <cstdint>

// ── WiFi Provisioning via QR + Captive Portal ──
// Creates a WiFi AP, shows QR code on screen, runs a captive portal
// where the user picks their home WiFi and enters the password.

// Start provisioning: creates AP, DNS, web server
void provisionStart(bool english = false);

// Stop provisioning: tears down AP, DNS, web server
void provisionStop();

// Call every loop iteration during provisioning
// Returns true when credentials have been received
bool provisionTick();

// Draw the QR code screen (call once after provisionStart)
void provisionDrawQR(bool english = false);

// Check if provisioning has received WiFi credentials
bool provisionHasCredentials();

// Get received credentials (valid after provisionHasCredentials() == true)
void provisionGetCredentials(char* ssid, int ssidLen, char* pass, int passLen);
