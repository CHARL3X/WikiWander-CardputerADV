#include "indicators.h"
#include "colors.h"
#include <M5Cardputer.h>
#include <WiFi.h>

namespace ui {

namespace {

// 3-bar WiFi glyph, 9 px wide, 8 px tall. Bar heights 3/5/7 px.
constexpr int kWifiW = 9;

// Battery glyph: 16x7 rect with a 2x3 tip, fill bar inside.
constexpr int kBattW = 18;

void drawWifi(M5Canvas& c, int x, int y) {
    int bars = 0;
    if (WiFi.status() == WL_CONNECTED) {
        int rssi = WiFi.RSSI();
        if      (rssi >= -55) bars = 3;
        else if (rssi >= -70) bars = 2;
        else if (rssi >= -85) bars = 1;
        else                  bars = 1;
    }
    // Bars left to right
    const int heights[3] = {3, 5, 7};
    for (int i = 0; i < 3; ++i) {
        int bw = 2;
        int bh = heights[i];
        int bx = x + i * 3;
        int by = y + 8 - bh;
        uint16_t col = (i < bars) ? kAccent : kAccentLo;
        c.fillRect(bx, by, bw, bh, col);
    }
}

void drawBattery(M5Canvas& c, int x, int y) {
    int pct = 100;
#ifdef M5UNIFIED_HPP
    pct = M5.Power.getBatteryLevel();
#else
    pct = M5Cardputer.Power.getBatteryLevel();
#endif
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    // Body
    constexpr int bodyW = 14;
    constexpr int bodyH = 7;
    c.drawRect(x, y + 1, bodyW, bodyH, kDim);
    c.fillRect(x + bodyW, y + 3, 2, 3, kDim);   // tip

    // Fill
    int fillW = (bodyW - 2) * pct / 100;
    uint16_t fc = kAccent;
    if (pct < 50) fc = kWarn;
    if (pct < 20) fc = kErr;
    if (fillW > 0) c.fillRect(x + 1, y + 2, fillW, bodyH - 2, fc);
}

} // namespace

int drawIndicators(M5Canvas& c, int rightEdgeX, int statusY) {
    int x = rightEdgeX - kBattW;
    drawBattery(c, x, statusY);
    x -= 4;
    x -= kWifiW;
    drawWifi(c, x, statusY + 2);
    return x - 2;  // hint to caller: keep labels to the left of this
}

} // namespace ui
