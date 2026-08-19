#pragma once

#include <stdint.h>
#include "touch_manager.h"

void usdtMockSetup();
void usdtMockTick(uint32_t nowMs);
void usdtMockHandleTouch(const TouchEvent& event);
