#include "boot_ui.h"
#include "colors.h"
#include <M5Cardputer.h>

namespace boot_ui {

void clear() {
    M5Cardputer.Display.fillScreen(ui::kBg);
}

void header(const String& title, uint16_t color) {
    M5Cardputer.Display.fillRect(0, 0, ui::kScreenW, 18, ui::kBg);
    M5Cardputer.Display.setFont(&fonts::FreeSerifBoldItalic9pt7b);
    M5Cardputer.Display.setTextSize(1);
    int midY = 9;
    int leftEnd = ui::kPadX + 10;
    M5Cardputer.Display.drawLine(ui::kPadX, midY, leftEnd, midY, color);
    M5Cardputer.Display.setTextDatum(top_left);
    M5Cardputer.Display.setTextColor(color, ui::kBg);
    M5Cardputer.Display.drawString(title, leftEnd + 4, 1);
    int tw = M5Cardputer.Display.textWidth(title);
    int rightStart = leftEnd + 4 + tw + 4;
    M5Cardputer.Display.drawLine(rightStart, midY,
                                 ui::kScreenW - ui::kPadX, midY, color);
    M5Cardputer.Display.setFont(&fonts::Font2);
}

void centerText(const String& line, int y, uint16_t color) {
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(color, ui::kBg);
    int tw = M5Cardputer.Display.textWidth(line);
    M5Cardputer.Display.setCursor((ui::kScreenW - tw) / 2, y);
    M5Cardputer.Display.print(line);
}

void footer(const String& hint) {
    int hy = ui::kScreenH - ui::kHintH;
    M5Cardputer.Display.fillRect(0, hy, ui::kScreenW, ui::kHintH, ui::kBg);
    M5Cardputer.Display.drawLine(0, hy, ui::kScreenW, hy, ui::kDivider);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(ui::kDim, ui::kBg);
    M5Cardputer.Display.setCursor(ui::kPadX, hy + 6);
    M5Cardputer.Display.print(hint);
    M5Cardputer.Display.setFont(&fonts::Font2);
}

void waitForAnyKey() {
    // Drain currently-held state
    M5Cardputer.update();
    while (M5Cardputer.Keyboard.isPressed()) {
        delay(15);
        M5Cardputer.update();
    }
    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isPressed()) break;
        delay(15);
    }
    // wait for release so caller's next read isn't immediate
    while (M5Cardputer.Keyboard.isPressed()) {
        delay(15);
        M5Cardputer.update();
    }
}

void halt(const String& head, const String& detail) {
    clear();
    header(head, ui::kErr);
    centerText(detail, 56, ui::kErr);
    footer("power cycle to retry");
    while (true) delay(1000);
}

} // namespace boot_ui
