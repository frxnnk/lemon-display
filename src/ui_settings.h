#pragma once

#include "touch_manager.h"

void settingsDraw();
void settingsHandleTouch(const TouchEvent& evt);
void settingsTick();

void settingsSetOtaAvailable(bool available);
bool settingsConsumeServiceRecovery();
