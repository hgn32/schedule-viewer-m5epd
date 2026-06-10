#include "display.h"
#include "time_util.h"
#include <algorithm>

#define C_BLACK  15
#define C_DGRAY  15
#define C_GRAY   15
#define C_LGRAY  15
#define C_WHITE  0

// https://mplusfonts.github.io/
// 太めが良い
static const char* FONT_PATH = "/MPLUS1-ExtraBold.ttf";
static const int FS_BOOT  = 36;
static const int FS_HEADER  = 32;
static const int FS_TITLE = 16;
static const int FS_LOC   = 12;

// ─────────────────────────────────────────────────────────────────────────────

void Display::begin() {
    // Official M5EPD_TTF order: loadFont BEFORE createCanvas
    esp_err_t err = _canvas.loadFont(FONT_PATH, SD);
    _canvas.createCanvas(SCR_W, SCR_H);
    _canvas.setTextDatum(TL_DATUM);

    if (err == ESP_OK) {
        Serial.printf("[FONT] Loaded %s\n", FONT_PATH);
        _canvas.useFreetypeFont(true);
        _canvas.createRender(FS_BOOT, 256);
        _canvas.createRender(FS_HEADER, 256);
        _canvas.createRender(FS_TITLE, 256);
        _canvas.createRender(FS_LOC, 256);
        _font_loaded = true;
    } else {
        Serial.printf("[FONT] TTF load failed (err=%d), using built-in font\n", (int)err);
        _canvas.setTextFont(2);
    }
    // No pushCanvas here; first visible update is done by showBootMessage().
}

void Display::showBootMessage(const String& msg) {
    _canvas.fillCanvas(C_WHITE);
    if (_font_loaded) {
        _canvas.setTextSize(FS_BOOT);
        _canvas.setTextDatum(MC_DATUM);
        _canvas.drawString(msg, SCR_W / 2, SCR_H / 2);
        _canvas.setTextDatum(TL_DATUM);
    } else {
        _canvas.setTextFont(2);
        _canvas.setTextSize(3);
        _canvas.setTextDatum(MC_DATUM);
        _canvas.drawString(msg, SCR_W / 2, SCR_H / 2);
        _canvas.setTextDatum(TL_DATUM);
    }
    // First visible push: INIT then GC16, matching m5ped-png-board's
    // proven panel-init sequence at 540x960.
    _canvas.pushCanvas(0, 0, UPDATE_MODE_INIT);
    _canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}

// ─────────────────────────────────────────────────────────────────────────────

void Display::render(ScheduleStore& store, uint32_t now_utc) {
    struct tm jst_now = nowJst();
    uint32_t display_start_utc = floorHourUtc();
    uint32_t display_end_utc   = display_start_utc + DISP_HOURS * 3600u;
    
    _canvas.fillCanvas(C_WHITE);

    drawHeader(jst_now);

    _canvas.drawLine(0, HEADER_H, SCR_W, HEADER_H, C_BLACK);
    _canvas.fillRect(LABEL_W, HEADER_H, 3, SCR_H - HEADER_H, C_GRAY);

    auto events = store.getInRange(display_start_utc, display_end_utc);
    drawTimeline(events, display_start_utc, now_utc);

    // GC16 full refresh, matching official M5EPD_TTF and png-board.
    _canvas.pushCanvas(0, 0, UPDATE_MODE_INIT); //ゴースト対策
    _canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}

// ─────────────────────────────────────────────────────────────────────────────

void Display::drawHeader(const struct tm& jst_now) {
    String date_str = formatDate(jst_now);
    String time_str = formatTime(jst_now);

    if (_font_loaded) {
        canvasText(date_str, 20, (HEADER_H - FS_HEADER) / 2, FS_HEADER);
        canvasTextRight(time_str, SCR_W - 20, (HEADER_H - FS_HEADER) / 2, FS_HEADER);
    } else {
        _canvas.setTextFont(2);
        _canvas.setTextSize(3);
        _canvas.setTextDatum(ML_DATUM);
        _canvas.drawString(date_str, 20, HEADER_H / 2);
        _canvas.setTextDatum(MR_DATUM);
        _canvas.drawString(time_str, SCR_W - 20, HEADER_H / 2);
        _canvas.setTextDatum(TL_DATUM);
    }
}

void Display::drawTimeline(const std::vector<Event>& events,
                           uint32_t display_start_utc, uint32_t now_utc) {
    const float px_hour = pxPerHour();
    const float px_min  = pxPerMin();

    for (int h = 0; h <= DISP_HOURS; h++) {
        int y = HEADER_H + (int)(h * px_hour);

        uint8_t line_col = (h == 0 || h == DISP_HOURS) ? C_BLACK : C_LGRAY;
        _canvas.drawLine(0, y, SCR_W, y, line_col);

        if (h < DISP_HOURS) {
            uint32_t hour_utc = display_start_utc + (uint32_t)(h * 3600);
            time_t jst_t = (time_t)(hour_utc + JST_OFFSET);
            struct tm t;
            gmtime_r(&jst_t, &t);
            String label = formatHour(t.tm_hour);

            if (_font_loaded) {
                canvasText(label, (LABEL_W - 40) / 2, y + 8, FS_HEADER);
            } else {
                _canvas.setTextFont(2);
                _canvas.setTextSize(2);
                _canvas.setTextDatum(TC_DATUM);
                _canvas.drawString(label, LABEL_W / 2, y + 8);
                _canvas.setTextDatum(TL_DATUM);
            }
        }
    }

    if (now_utc >= display_start_utc &&
        now_utc <  display_start_utc + (uint32_t)(DISP_HOURS * 3600)) {
        int elapsed_min = (int)((now_utc - display_start_utc) / 60);
        int y_now = HEADER_H + (int)(elapsed_min * px_min);
        _canvas.fillCircle(LABEL_W, y_now, 8, C_DGRAY);
        _canvas.drawLine(LABEL_W, y_now, SCR_W, y_now, C_DGRAY);
    }

    auto layout = layoutEvents(events);
    for (const auto& le : layout) {
        drawEventBox(le, display_start_utc);
    }
}

void Display::drawEventBox(const LayoutEvent& le, uint32_t display_start_utc) {
    const float px_min    = pxPerMin();
    const int   col_w     = CONTENT_W / le.total_cols;
    const int   pad       = 3;

    uint32_t disp_end_utc = display_start_utc + (uint32_t)(DISP_HOURS * 3600);
    uint32_t clipped_start = std::max(le.event.start_utc, display_start_utc);
    uint32_t clipped_end   = std::min(le.event.end_utc,   disp_end_utc);
    if (clipped_start >= clipped_end) return;

    int start_min = (int)((clipped_start - display_start_utc) / 60);
    int end_min   = (int)((clipped_end   - display_start_utc) / 60);
    int y_top     = HEADER_H + (int)(start_min * px_min);
    int y_bot     = HEADER_H + (int)(end_min   * px_min);
    int x_left    = CONTENT_X + le.col * col_w + pad;
    int x_right   = CONTENT_X + (le.col + 1) * col_w - pad;
    int box_h     = y_bot - y_top;
    int box_w     = x_right - x_left;

    if (box_h < 4 || box_w < 4) return;

    _canvas.fillRoundRect(x_left, y_top, box_w, box_h, 6, C_WHITE);
    _canvas.drawRoundRect(x_left, y_top, box_w, box_h, 6, C_BLACK);

    int tx = x_left + 6;
    int ty = y_top + 6;

    if (box_h >= FS_TITLE + 10) {
        if (_font_loaded) {
            canvasText(le.event.title, tx, ty, FS_TITLE);
        } else {
            _canvas.setTextFont(2);
            _canvas.setTextSize(2);
            _canvas.setTextDatum(TL_DATUM);
            _canvas.drawString(le.event.title, tx, ty);
        }
    }
    if (box_h >= FS_TITLE + FS_LOC + 18 && le.event.location.length() > 0) {
        int ly = ty + FS_TITLE + 4;
        if (_font_loaded) {
            canvasText(le.event.location, tx, ly, FS_LOC, C_DGRAY);
        } else {
            _canvas.setTextFont(2);
            _canvas.setTextSize(1);
            _canvas.drawString(le.event.location, tx, ly);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

std::vector<LayoutEvent> Display::layoutEvents(std::vector<Event> events) {
    std::vector<int>      cols(events.size(), 0);
    std::vector<uint32_t> col_ends;

    for (size_t i = 0; i < events.size(); i++) {
        int c = 0;
        while (c < (int)col_ends.size() && col_ends[c] > events[i].start_utc) {
            c++;
        }
        cols[i] = c;
        if (c < (int)col_ends.size()) {
            col_ends[c] = events[i].end_utc;
        } else {
            col_ends.push_back(events[i].end_utc);
        }
    }

    std::vector<int> totals(events.size(), 1);
    for (size_t i = 0; i < events.size(); i++) {
        for (size_t j = 0; j < events.size(); j++) {
            bool overlaps = events[j].start_utc < events[i].end_utc &&
                            events[j].end_utc   > events[i].start_utc;
            if (overlaps) {
                totals[i] = std::max(totals[i], cols[j] + 1);
            }
        }
    }

    std::vector<LayoutEvent> result;
    result.reserve(events.size());
    for (size_t i = 0; i < events.size(); i++) {
        result.push_back({events[i], cols[i], totals[i]});
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────

void Display::canvasText(const String& str, int x, int y, int size_px, uint8_t color) {
    _canvas.setTextSize(size_px);
    _canvas.setTextDatum(TL_DATUM);
    // Faux-bold: draw twice with a 1px horizontal offset (font renders thin on this panel)
    _canvas.drawString(str, x, y);
    _canvas.drawString(str, x + 1, y);
}

void Display::canvasTextRight(const String& str, int right_x, int y, int size_px, uint8_t color) {
    _canvas.setTextSize(size_px);
    _canvas.setTextDatum(TR_DATUM);
    _canvas.drawString(str, right_x, y);
    _canvas.drawString(str, right_x + 1, y);
}
