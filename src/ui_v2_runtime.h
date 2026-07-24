#pragma once

#include "touch_manager.h"
#include "v2_runtime.h"

void v2UiSetup();
void v2UiDrawLoading(const char* status, uint8_t progress);
void v2UiDraw(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateClock(const char* time, V2Scene scene);
void v2UiUpdateStatus(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdatePair(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateHomeCards(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateTape(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateContext(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateData(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
