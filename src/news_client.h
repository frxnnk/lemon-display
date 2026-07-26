#pragma once

#include "data_models.h"

bool newsFetch(const char* symbol, StockNews* out, uint8_t maxItems, uint8_t& count);
bool newsHasCache(const char* symbol);
bool newsCacheIsFresh(const char* symbol);
void newsGetCached(const char* symbol, StockNews* out, uint8_t maxItems, uint8_t& count);
void newsClearCache();
