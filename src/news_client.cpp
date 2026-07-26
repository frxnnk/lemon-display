#include "news_client.h"
#include "api_client.h"
#include <Arduino.h>
#include <cstring>

static const uint32_t NEWS_CACHE_TTL_MS = 10UL * 60UL * 1000UL;

struct NewsCacheEntry {
    char symbol[STOCK_SYMBOL_LEN];
    StockNews news[NEWS_MAX_ITEMS];
    uint8_t count;
    uint32_t cachedMs;
};

static NewsCacheEntry _newsCache[STOCK_MAX_SYMBOLS];
static uint8_t _nextCacheSlot = 0;

static NewsCacheEntry* findCache(const char* symbol) {
    if (!symbol || !symbol[0]) return nullptr;
    for (uint8_t i = 0; i < STOCK_MAX_SYMBOLS; i++) {
        if (_newsCache[i].count > 0 &&
            strncmp(_newsCache[i].symbol, symbol, STOCK_SYMBOL_LEN) == 0) {
            return &_newsCache[i];
        }
    }
    return nullptr;
}

static NewsCacheEntry* reserveCache(const char* symbol) {
    NewsCacheEntry* existing = findCache(symbol);
    if (existing) return existing;
    for (uint8_t i = 0; i < STOCK_MAX_SYMBOLS; i++) {
        if (_newsCache[i].count == 0) return &_newsCache[i];
    }
    NewsCacheEntry* replacement = &_newsCache[_nextCacheSlot];
    _nextCacheSlot = static_cast<uint8_t>(
        (_nextCacheSlot + 1) % STOCK_MAX_SYMBOLS);
    return replacement;
}

void newsClearCache() {
    memset(_newsCache, 0, sizeof(_newsCache));
    _nextCacheSlot = 0;
}

bool newsHasCache(const char* symbol) {
    return findCache(symbol) != nullptr;
}

bool newsCacheIsFresh(const char* symbol) {
    NewsCacheEntry* cache = findCache(symbol);
    return cache && millis() - cache->cachedMs < NEWS_CACHE_TTL_MS;
}

void newsGetCached(const char* symbol, StockNews* out, uint8_t maxItems, uint8_t& count) {
    NewsCacheEntry* cache = findCache(symbol);
    if (!cache) { count = 0; return; }
    count = cache->count < maxItems ? cache->count : maxItems;
    for (uint8_t i = 0; i < count; i++) out[i] = cache->news[i];
}

static void copyText(char* dst, size_t dstLen, const char* start, const char* end) {
    size_t n = end - start;
    if (n >= dstLen) n = dstLen - 1;
    memcpy(dst, start, n);
    dst[n] = '\0';
    for (size_t i = 0; i < n; i++) {
        if (dst[i] == '\n' || dst[i] == '\r' || dst[i] == '\t') dst[i] = ' ';
    }
}

static void decodeEntities(char* s) {
    char* src = s;
    char* dst = s;
    while (*src) {
        if (src[0] == '&' && src[1] == '#') {
            char* semi = strchr(src + 2, ';');
            if (semi) {
                src = semi + 1;
                *dst++ = ' ';
                continue;
            }
        }
        if (src[0] == '&') {
            char* semi = strchr(src + 1, ';');
            if (semi && (semi - src) < 8) {
                if (strncmp(src, "&amp;", 5) == 0) { *dst++ = '&'; src = semi + 1; continue; }
                if (strncmp(src, "&lt;", 4) == 0) { *dst++ = '<'; src = semi + 1; continue; }
                if (strncmp(src, "&gt;", 4) == 0) { *dst++ = '>'; src = semi + 1; continue; }
                if (strncmp(src, "&quot;", 6) == 0) { *dst++ = '"'; src = semi + 1; continue; }
                if (strncmp(src, "&apos;", 6) == 0) { *dst++ = '\''; src = semi + 1; continue; }
                src = semi + 1;
                continue;
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

static void normalizeNewsPunctuation(char* s) {
    const unsigned char* src = reinterpret_cast<const unsigned char*>(s);
    char* dst = s;
    while (*src) {
        if (*src < 0x80) {
            *dst++ = static_cast<char>(*src++);
            continue;
        }
        if (src[0] == 0xE2 && src[1] && src[2] && src[1] == 0x80) {
            if (src[2] == 0x98 || src[2] == 0x99) {
                *dst++ = '\'';
                src += 3;
                continue;
            }
            if (src[2] == 0x9C || src[2] == 0x9D) {
                *dst++ = '"';
                src += 3;
                continue;
            }
            if (src[2] == 0x93 || src[2] == 0x94) {
                *dst++ = '-';
                src += 3;
                continue;
            }
            if (src[2] == 0xA6) {
                memcpy(dst, "...", 3);
                dst += 3;
                src += 3;
                continue;
            }
        }
        if (src[0] == 0xC2 && src[1] && src[1] == 0xA0) {
            *dst++ = ' ';
            src += 2;
            continue;
        }

        // The embedded news font is ASCII-only. Drop unsupported codepoints
        // instead of letting their UTF-8 bytes render as blank glyphs.
        if ((*src & 0xE0) == 0xC0 && src[1]) src += 2;
        else if ((*src & 0xF0) == 0xE0 && src[1] && src[2]) src += 3;
        else if ((*src & 0xF8) == 0xF0 && src[1] && src[2] && src[3]) src += 4;
        else src++;
    }
    *dst = '\0';
}

static void stripHtml(char* s) {
    char* src = s;
    char* dst = s;
    bool inTag = false;
    while (*src) {
        if (*src == '<') { inTag = true; src++; continue; }
        if (*src == '>') { inTag = false; src++; continue; }
        if (!inTag) *dst++ = *src;
        src++;
    }
    *dst = '\0';
    decodeEntities(s);
    normalizeNewsPunctuation(s);
    char* trim = s;
    while (*trim == ' ') trim++;
    if (trim != s) memmove(s, trim, strlen(trim) + 1);
    dst = s + strlen(s);
    while (dst > s && (dst[-1] == ' ' || dst[-1] == '\n')) *--dst = '\0';
}

bool newsFetch(const char* symbol, StockNews* out, uint8_t maxItems, uint8_t& count) {
    count = 0;
    if (!symbol || !symbol[0]) return false;

    if (newsCacheIsFresh(symbol)) {
        newsGetCached(symbol, out, maxItems, count);
        return count > 0;
    }

    char url[192];
    snprintf(url, sizeof(url),
             "https://news.google.com/rss/search?q=%s+stock&hl=en-US&gl=US&ceid=US:en",
             symbol);

    ApiResult result = API_NETWORK_ERROR;
    const char* xml = apiHttpGet(url, false, result, 12000);
    if (result != API_OK || !xml || !xml[0]) {
        Serial.printf("[News] request failed (%d); keeping cache\n",
                      static_cast<int>(result));
        newsGetCached(symbol, out, maxItems, count);
        return count > 0;
    }

    uint8_t found = 0;
    const char* cursor = xml;
    while (found < maxItems && found < NEWS_MAX_ITEMS) {
        const char* item = strstr(cursor, "<item>");
        if (!item) break;
        const char* itemEnd = strstr(item, "</item>");
        if (!itemEnd) break;

        const char* titleStart = strstr(item, "<title>");
        if (!titleStart || titleStart > itemEnd) { cursor = itemEnd + 7; continue; }
        titleStart += 7;
        const char* titleEnd = strstr(titleStart, "</title>");
        if (!titleEnd || titleEnd > itemEnd) { cursor = itemEnd + 7; continue; }
        copyText(out[found].title, NEWS_TITLE_LEN, titleStart, titleEnd);
        stripHtml(out[found].title);

        const char* dateStart = strstr(item, "<pubDate>");
        if (dateStart && dateStart < itemEnd) {
            dateStart += 9;
            const char* dateEnd = strstr(dateStart, "</pubDate>");
            if (dateEnd && dateEnd < itemEnd) {
                copyText(out[found].pubDate, NEWS_DATE_LEN, dateStart, dateEnd);
            } else { out[found].pubDate[0] = '\0'; }
        } else { out[found].pubDate[0] = '\0'; }

        out[found].valid = strlen(out[found].title) > 0;
        if (out[found].valid) found++;
        cursor = itemEnd + 7;
    }

    if (found == 0) {
        newsGetCached(symbol, out, maxItems, count);
        return count > 0;
    }

    count = found;
    NewsCacheEntry* cache = reserveCache(symbol);
    memset(cache, 0, sizeof(*cache));
    cache->count = found;
    cache->cachedMs = millis();
    strncpy(cache->symbol, symbol, STOCK_SYMBOL_LEN - 1);
    cache->symbol[STOCK_SYMBOL_LEN - 1] = '\0';
    for (uint8_t i = 0; i < found; i++) cache->news[i] = out[i];

    Serial.printf("[News] %s: %d headlines\n", symbol, found);
    return found > 0;
}
