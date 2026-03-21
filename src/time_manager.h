#pragma once

#include <Arduino.h>

void timeSetup();
bool timeReady();
const char* getTimeStr(bool use24h = true);    // 24h: "HH:MM:SS", 12h: "H:MM PM"
const char* getDateStr();    // "DD/MM/YYYY"
