#include "display.h"
#include "time_util.h"
#include <algorithm>

// M5EPD 4bpp grayscale: 0 = black, 15 = white
#define C_BLACK  0
#define C_DGRAY  3
#define C_GRAY   7
#define C_LGRAY  11
#define C_WHITE  15

// OpenFontRender RGB565 colors
#define OFR_BLACK  0x0000u
#define OFR_DGRAY  0x4208u
#define OFR_WHITE  0xFFFFu

static const char* FONT_PATH = "/NotoSansJP-VariableFont_wght.ttf";

// Font sizes in pixels
static const int FS_DATE  = 52;
static const int FS_TIME  = 64;
static const int FS_HOUR  = 38;
static const int FS_TITLE = 30;
static const int FS_LOC   = 24;

// ─────────────────────────────────────────────────────────────────────────────

void Display::begin() {
    _canvas.createCanvas(SCR_W, SCR_H);
    _canvas.setTextDatum(TL_DATUM);

    // Show first boot message using built-in font before TTF is loaded
    _canvas.fillCanvas(C_WHITE);
    _canvas.setTextColor(C_BLACK);
    _canvas.setFont(&fonts::efontJA_16);
    _canvas.setTextSize(3);
    _canvas.setTextDatum(MC_DATUM);
    _canvas.drawString("初期化中...", SCR_W / 2, SCR_H / 2);
    _canvas.setTextDatum(TL_DATUM);
    _canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);

    // Load NotoSansJP TTF from SD card
    _ofr.setSerial(Serial);
    _ofr.setDrawer(_canvas);

    FT_Error err = _ofr.loadFont(FONT_PATH, SD);
    if (err == 0) {
        Serial.printf("[FONT] Loaded %s\n", FONT_PATH);
        _font_loaded = true;
    } else {
        Serial.printf("[FONT] TTF load failed (err=%d), using efontJA\n", (int)err);
        _canvas.setFont(&fonts::efontJA_16);
    }
}

void Display::showBootMessage(const String& msg) {
    _canvas.fillCanvas(C_WHITE);
    _canvas.setTextColor(C_BLACK);
    if (_font_loaded) {
        ofrText(msg, SCR_W / 2 - 200, SCR_H / 2 - 40, FS_DATE);
    } else {
        _canvas.setFont(&fonts::efontJA_16);
        _canvas.setTextSize(3);
        _canvas.setTextDatum(MC_DATUM);
        _canvas.drawString(msg, SCR_W / 2, SCR_H / 2);
        _canvas.setTextDatum(TL_DATUM);
    }
    _canvas.pushCanvas(0, 0, UPDATE_MODE_DU);
}

// ─────────────────────────────────────────────────────────────────────────────

void Display::render(ScheduleStore& store, uint32_t now_utc) {
    struct tm jst_now = nowJst();
    uint32_t display_start_utc = floorHourUtc();
    uint32_t display_end_utc   = display_start_utc + DISP_HOURS * 3600u;

    _canvas.fillCanvas(C_WHITE);

    drawHeader(jst_now);

    // Separator under header
    _canvas.drawLine(0, HEADER_H, SCR_W, HEADER_H, C_BLACK);
    // Vertical separator between time labels and events
    _canvas.drawLine(LABEL_W, HEADER_H, LABEL_W, SCR_H, C_GRAY);

    auto events = store.getInRange(display_start_utc, display_end_utc);
    drawTimeline(events, display_start_utc, now_utc);

    _canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}

// ─────────────────────────────────────────────────────────────────────────────

void Display::drawHeader(const struct tm& jst_now) {
    String date_str = formatDate(jst_now);
    String time_str = formatTime(jst_now);

    if (_font_loaded) {
        ofrText(date_str, 20, (HEADER_H - FS_DATE) / 2, FS_DATE);
        ofrTextRight(time_str, SCR_W - 20, (HEADER_H - FS_TIME) / 2, FS_TIME);
    } else {
        _canvas.setFont(&fonts::efontJA_16);
        _canvas.setTextColor(C_BLACK);
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

    // Hour grid lines and labels
    for (int h = 0; h <= DISP_HOURS; h++) {
        int y = HEADER_H + (int)(h * px_hour);

        // Horizontal grid line
        uint8_t line_col = (h == 0 || h == DISP_HOURS) ? C_BLACK : C_LGRAY;
        _canvas.drawLine(0, y, SCR_W, y, line_col);

        if (h < DISP_HOURS) {
            uint32_t hour_utc = display_start_utc + (uint32_t)(h * 3600);
            time_t jst_t = (time_t)(hour_utc + JST_OFFSET);
            struct tm t;
            gmtime_r(&jst_t, &t);
            String label = formatHour(t.tm_hour);

            if (_font_loaded) {
                int lx = (LABEL_W - 40) / 2;  // centered in label column
                int ly = y + 8;
                ofrText(label, lx, ly, FS_HOUR);
            } else {
                _canvas.setFont(&fonts::efontJA_16);
                _canvas.setTextSize(2);
                _canvas.setTextColor(C_BLACK);
                _canvas.setTextDatum(TC_DATUM);
                _canvas.drawString(label, LABEL_W / 2, y + 8);
                _canvas.setTextDatum(TL_DATUM);
            }
        }
    }

    // Current-time marker line
    if (now_utc >= display_start_utc &&
        now_utc <  display_start_utc + (uint32_t)(DISP_HOURS * 3600)) {
        int elapsed_min = (int)((now_utc - display_start_utc) / 60);
        int y_now = HEADER_H + (int)(elapsed_min * px_min);
        // Small circle at left edge of content area
        _canvas.fillCircle(LABEL_W, y_now, 8, C_DGRAY);
        _canvas.drawLine(LABEL_W, y_now, SCR_W, y_now, C_DGRAY);
    }

    // Event boxes
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

    // Box: white fill, black border
    _canvas.fillRect(x_left, y_top, box_w, box_h, C_WHITE);
    _canvas.drawRect(x_left, y_top, box_w, box_h, C_BLACK);

    // Text inside box
    int tx = x_left + 6;
    int ty = y_top + 6;

    if (box_h >= FS_TITLE + 10) {
        if (_font_loaded) {
            ofrText(le.event.title, tx, ty, FS_TITLE);
        } else {
            _canvas.setFont(&fonts::efontJA_16);
            _canvas.setTextSize(2);
            _canvas.setTextColor(C_BLACK);
            _canvas.setTextDatum(TL_DATUM);
            _canvas.drawString(le.event.title, tx, ty);
        }
    }
    if (box_h >= FS_TITLE + FS_LOC + 18 && le.event.location.length() > 0) {
        int ly = ty + FS_TITLE + 4;
        if (_font_loaded) {
            ofrText(le.event.location, tx, ly, FS_LOC, OFR_DGRAY);
        } else {
            _canvas.setFont(&fonts::efontJA_16);
            _canvas.setTextSize(1);
            _canvas.setTextColor(C_DGRAY);
            _canvas.drawString(le.event.location, tx, ly);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

std::vector<LayoutEvent> Display::layoutEvents(std::vector<Event> events) {
    // Greedy column assignment: assign each event the smallest column
    // whose last event has already ended.
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

    // For each event, total_cols = max col index among overlapping events + 1
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

void Display::ofrText(const String& str, int x, int y, int size_px, uint16_t color) {
    _ofr.setFontSize(size_px);
    _ofr.setFontColor(color, OFR_WHITE);
    _ofr.drawString(str.c_str(), x, y);
}

void Display::ofrTextRight(const String& str, int right_x, int y, int size_px, uint16_t color) {
    _ofr.setFontSize(size_px);
    _ofr.setFontColor(color, OFR_WHITE);
    // Estimate width: average ~0.6× font height per character for this font
    int est_w = (int)(str.length() * size_px * 0.6f);
    int x = right_x - est_w;
    if (x < 0) x = 0;
    _ofr.drawString(str.c_str(), x, y);
}
