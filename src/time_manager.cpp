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

String getTimeStr(bool use24h) {
    struct tm t;
    if (!getLocalTime(&t, 0)) return use24h ? "--:--:--" : "--:-- --";
    char buf[12];
    if (use24h) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        int h = t.tm_hour % 12;
        if (h == 0) h = 12;
        const char* ampm = (t.tm_hour >= 12) ? "PM" : "AM";
        snprintf(buf, sizeof(buf), "%02d:%02d %s", h, t.tm_min, ampm);
    }
    return String(buf);
}

String getDateStr() {
    struct tm t;
    if (!getLocalTime(&t, 0)) return "--/--/----";
    char buf[11];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    return String(buf);
}
