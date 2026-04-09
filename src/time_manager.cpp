#include "time_manager.h"
#include "config.h"
#include <time.h>

static bool synced = false;

void timeSetup() {
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER);
    Serial.println("[Time] NTP sync started (UTC-3 Argentina)");
}

bool timeReady() {
    if (synced) return true;
    struct tm t;
    if (getLocalTime(&t, 0)) {
        synced = true;
        Serial.printf("[Time] Synced: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
    }
    return synced;
}

// Cache last valid time string to prevent flicker on brief getLocalTime() failures
static char lastTimeStr[12] = "--:--:--";

const char* getTimeStr(bool use24h) {
    struct tm t;
    if (!getLocalTime(&t, 0)) {
        if (synced) return lastTimeStr;
        return use24h ? "--:--:--" : "--:-- --";
    }
    if (use24h) {
        snprintf(lastTimeStr, sizeof(lastTimeStr), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        int h = t.tm_hour % 12;
        if (h == 0) h = 12;
        const char* ampm = (t.tm_hour >= 12) ? "PM" : "AM";
        snprintf(lastTimeStr, sizeof(lastTimeStr), "%02d:%02d %s", h, t.tm_min, ampm);
    }
    return lastTimeStr;
}

static char lastDateStr[11] = "--/--/----";

const char* getDateStr() {
    struct tm t;
    if (!getLocalTime(&t, 0)) return lastDateStr;
    snprintf(lastDateStr, sizeof(lastDateStr), "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    return lastDateStr;
}
