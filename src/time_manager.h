#pragma once

#include <Arduino.h>

void timeSetup();
bool timeReady();
String getTimeStr(bool use24h = true);    // 24h: "HH:MM:SS", 12h: "H:MM PM"
String getDateStr();    // "DD/MM/YYYY"
