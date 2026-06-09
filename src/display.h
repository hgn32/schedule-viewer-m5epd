#pragma once
#include <M5EPD.h>
#include <OpenFontRender.h>
#include "schedule.h"

struct LayoutEvent {
    Event event;
    int col;
    int total_cols;
};

class Display {
public:
    // Call once in setup() after M5.begin(). Shows "初期化中..." and loads font.
    void begin();

    // Full-screen message (used during boot stages)
    void showBootMessage(const String& msg);

    // Render timeline for the 6 hours starting from floor(now) in JST
    void render(ScheduleStore& store, uint32_t now_utc);

private:
    M5EPD_Canvas  _canvas;
    OpenFontRender _ofr;
    bool _font_loaded = false;

    void drawHeader(const struct tm& jst_now);
    void drawTimeline(const std::vector<Event>& events, uint32_t display_start_utc,
                      uint32_t now_utc);
    void drawEventBox(const LayoutEvent& le, uint32_t display_start_utc);
    std::vector<LayoutEvent> layoutEvents(std::vector<Event> events);

    void ofrText(const String& str, int x, int y, int size_px, uint16_t color = 0x0000);
    void ofrTextRight(const String& str, int right_x, int y, int size_px, uint16_t color = 0x0000);

    // Screen dimensions (portrait, SetRotation(90))
    static const int SCR_W      = 1404;
    static const int SCR_H      = 1872;
    static const int HEADER_H   = 120;
    static const int TIMELINE_H = SCR_H - HEADER_H;  // 1752
    static const int LABEL_W    = 120;
    static const int CONTENT_X  = LABEL_W;
    static const int CONTENT_W  = SCR_W - LABEL_W;   // 1284
    static const int DISP_HOURS = 6;

    float pxPerHour() const { return (float)TIMELINE_H / DISP_HOURS; }
    float pxPerMin()  const { return pxPerHour() / 60.0f; }
};
