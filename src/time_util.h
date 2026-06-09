#pragma once
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>

static constexpr int JST_OFFSET = 32400; // UTC+9 in seconds

static const char* const WEEKDAYS_JA[] = {"日","月","火","水","木","金","土"};

// Set ESP32 system clock from UTC epoch
inline void setSystemTime(uint32_t utc_epoch) {
    struct timeval tv = {(time_t)utc_epoch, 0};
    settimeofday(&tv, nullptr);
}

// Current UTC epoch from system clock
inline uint32_t nowUtc() {
    return (uint32_t)time(nullptr);
}

// Current JST time as struct tm
inline struct tm nowJst() {
    time_t jst_t = (time_t)(nowUtc() + JST_OFFSET);
    struct tm t;
    gmtime_r(&jst_t, &t);
    return t;
}

// UTC epoch of the current hour boundary (floor to JST hour, return as UTC)
inline uint32_t floorHourUtc() {
    uint32_t jst_now = nowUtc() + JST_OFFSET;
    uint32_t jst_floored = (jst_now / 3600) * 3600;
    return jst_floored - JST_OFFSET;
}

// "2026/06/09(月)"
inline String formatDate(const struct tm& t) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d(%s)",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             WEEKDAYS_JA[t.tm_wday]);
    return String(buf);
}

// "14:00"
inline String formatTime(const struct tm& t) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return String(buf);
}

// "14" (hour only, zero-padded)
inline String formatHour(int hour) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", hour % 24);
    return String(buf);
}
