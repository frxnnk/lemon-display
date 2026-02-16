#pragma once

#include <Arduino.h>

void timeSetup();
bool timeReady();
String getTimeStr();    // "HH:MM:SS"
String getDateStr();    // "DD/MM/YYYY"
