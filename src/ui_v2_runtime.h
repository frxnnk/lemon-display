#pragma once

#include "touch_manager.h"
#include "v2_runtime.h"

void v2UiSetTheme(uint8_t theme);
bool v2UiIsLightTheme();
uint16_t v2UiColorBg();
uint16_t v2UiColorText();
uint16_t v2UiColorAccent();
uint16_t v2UiColorHighlight();
uint16_t v2UiColorMuted();
uint16_t v2UiColorLabelBg();
uint16_t v2UiColorLabelFg();
void v2UiSetup();
void v2UiDrawLoading(const char* status, uint8_t progress);
void v2UiDraw(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model, bool clearFull = true);
void v2UiUpdateClock(const char* time, V2Scene scene);
void v2UiUpdateHeader(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateStockHero(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateStockPrice(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateBtcCard(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateBtcPrice(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateDollarCard(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateNews(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateStockCard(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateHomeCards(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateTape(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateContext(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdateData(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdatePriceOnly(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiUpdatePriceDirect(const V2RuntimeSnapshot& snapshot, const V2RuntimeModel& model);
void v2UiSetDeferred(bool defer);
void v2UiFlushDeferred();
