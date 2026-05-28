#include "splash.h"
#include "colors.h"
#include <M5Cardputer.h>

namespace splash {

namespace {

void drawFrame(M5Canvas& c, int phase) {
    c.fillScreen(ui::kBg);

    // Brand: serif italic title in sepia, centered, with two hairlines
    // flanking like an old book title page. Subtitle dim below.
    c.setFont(&fonts::FreeSerifBoldItalic9pt7b);
    c.setTextSize(1);
    const char* title = "Wikiwander";
    c.setTextColor(ui::kAccent, ui::kBg);
    int tw = c.textWidth(title);
    int titleY = ui::kScreenH / 2 - 18;
    c.setTextDatum(top_left);
    c.setCursor((ui::kScreenW - tw) / 2, titleY);
    c.print(title);

    int lineY = titleY + 9;
    int gap = 12;
    int sweep = phase % 60;
    int reach = (sweep < 30 ? sweep : 60 - sweep) * 2;  // 0..58..0
    int leftEnd  = (ui::kScreenW - tw) / 2 - gap;
    int rightStart = (ui::kScreenW + tw) / 2 + gap;
    c.drawLine(leftEnd - reach,  lineY, leftEnd,  lineY, ui::kAccentLo);
    c.drawLine(rightStart, lineY, rightStart + reach, lineY, ui::kAccentLo);

    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(ui::kDim, ui::kBg);
    const char* sub = "a small pocket library";
    int sw = c.textWidth(sub);
    c.setCursor((ui::kScreenW - sw) / 2, titleY + 26);
    c.print(sub);
}

} // namespace

void run() {
    M5Canvas c(&M5Cardputer.Display);
    c.setColorDepth(16);
    bool ok = c.createSprite(ui::kScreenW, ui::kScreenH);
    if (!ok) {
        // Fallback: static draw, no canvas
        M5Cardputer.Display.fillScreen(ui::kBg);
        M5Cardputer.Display.setFont(&fonts::FreeSerifBoldItalic9pt7b);
        M5Cardputer.Display.setTextColor(ui::kAccent, ui::kBg);
        const char* t = "Wikiwander";
        int tw = M5Cardputer.Display.textWidth(t);
        M5Cardputer.Display.setCursor((ui::kScreenW - tw) / 2,
                                       ui::kScreenH / 2 - 9);
        M5Cardputer.Display.print(t);
        delay(900);
        return;
    }
    uint32_t t0 = millis();
    int phase = 0;
    while (millis() - t0 < 900) {
        drawFrame(c, phase++);
        c.pushSprite(0, 0);
        delay(30);
    }
    c.deleteSprite();
}

} // namespace splash
