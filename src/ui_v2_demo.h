#pragma once

#include <stdint.h>
#include "touch_manager.h"


void v2DemoSetup();
void v2DemoTick(uint32_t nowMs);
void v2DemoHandleTouch(const TouchEvent& event);
