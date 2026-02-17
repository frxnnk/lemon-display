#pragma once

#include "data_models.h"

// Call after WiFi + NTP are ready
void wsBinanceSetup();

// Must be called every loop iteration
void wsBinanceLoop();

// Graceful disconnect
void wsBinanceStop();

// Connection status
bool wsBinanceConnected();

// Latest price from WebSocket stream
float wsBinanceGetPrice();
bool  wsBinanceHasPrice();

// Copy circular buffer into linear SparklineData (oldest -> newest)
void wsBinanceGetSparkline(SparklineData& out);

// HTTP fetch 96 x 1m klines to fill buffer at boot (blocking)
bool wsBinanceBackfill();

// Parameterized variants for pair switching
void wsBinanceReconnect(const char* wsPath, bool invertPrices);
bool wsBinanceBackfillSymbol(const char* symbol, bool invert);
void wsBinanceSetInvert(bool invert);
