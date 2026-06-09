#include <M5EPD.h>
#include "time_util.h"
#include "schedule.h"
#include "protocol.h"
#include "display.h"

static ScheduleStore g_store;
static Protocol      g_proto(&g_store);
static Display       g_display;

static uint32_t g_last_hour_utc = 0;

// ─────────────────────────────────────────────────────────────────────────────

static void restoreTimeFromRTC() {
    rtc_date_t date;
    rtc_time_t rtc_time;
    M5.RTC.getDate(&date);
    M5.RTC.getTime(&rtc_time);

    if (date.year < 2024) return; // RTC not yet set

    // RTC stores JST as wall-clock time.
    // mktime (UTC TZ on ESP32) treats input as UTC, so we subtract JST_OFFSET.
    struct tm t = {};
    t.tm_year = date.year - 1900;
    t.tm_mon  = date.mon  - 1;
    t.tm_mday = date.day;
    t.tm_hour = rtc_time.hour;
    t.tm_min  = rtc_time.min;
    t.tm_sec  = rtc_time.sec;
    uint32_t utc_epoch = (uint32_t)mktime(&t) - JST_OFFSET;
    setSystemTime(utc_epoch);
    Serial.printf("[RTC] Restored time: %04d/%02d/%02d %02d:%02d\n",
                  date.year, date.mon, date.day, rtc_time.hour, rtc_time.min);
}

static void syncRTC(uint32_t utc_epoch) {
    // Store JST time in RTC (wall clock)
    time_t jst_t = (time_t)(utc_epoch + JST_OFFSET);
    struct tm t;
    gmtime_r(&jst_t, &t);
    rtc_time_t rt = {(uint8_t)t.tm_sec, (uint8_t)t.tm_min, (uint8_t)t.tm_hour};
    rtc_date_t rd = {(uint8_t)t.tm_wday, (uint8_t)t.tm_mday,
                     (uint8_t)(t.tm_mon + 1), (uint16_t)(t.tm_year + 1900)};
    M5.RTC.setTime(&rt);
    M5.RTC.setDate(&rd);
}

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    M5.begin();
    M5.EPD.SetRotation(90);
    M5.EPD.Clear(true);
    delay(3000);  // wait for EPD full-refresh to complete before next pushCanvas
    M5.RTC.begin();

    restoreTimeFromRTC();

    // begin() shows "初期化中..." and loads font from SD
    g_display.begin();

    g_display.showBootMessage("スケジュール要求中...");
    Serial.begin(115200);
    delay(100);
    Serial.println("REQ:ALL");
    Serial.flush();

    g_last_hour_utc = floorHourUtc();
    g_display.render(g_store, nowUtc());
}

void loop() {
    // Read incoming lines from USB serial (transport layer)
    while (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        g_proto.processLine(line);

        if (g_proto.isComplete()) {
            g_proto.resetComplete();

            uint32_t recv_time = g_proto.getReceivedTime();
            if (recv_time > 0) {
                setSystemTime(recv_time);
                syncRTC(recv_time);
            }

            g_display.render(g_store, nowUtc());
            g_last_hour_utc = floorHourUtc();
        }
    }

    // Re-render when the display hour window shifts
    uint32_t current_hour = floorHourUtc();
    if (current_hour != g_last_hour_utc) {
        g_last_hour_utc = current_hour;
        g_display.render(g_store, nowUtc());
    }

    delay(500);
}
