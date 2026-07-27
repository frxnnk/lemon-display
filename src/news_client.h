#pragma once

#include "data_models.h"

enum NewsFetchResult : uint8_t {
    NEWS_FETCH_UPDATED,
    NEWS_FETCH_FRESH_CACHE,
    NEWS_FETCH_STALE_CACHE,
    NEWS_FETCH_FAILED,
};

NewsFetchResult newsFetch(const char* symbol, StockNews* out, uint8_t maxItems,
                          uint8_t& count, bool forceRefresh = false);
bool newsHasCache(const char* symbol);
bool newsCacheIsFresh(const char* symbol);
void newsGetCached(const char* symbol, StockNews* out, uint8_t maxItems, uint8_t& count);
void newsClearCache();
