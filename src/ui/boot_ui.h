// Minimal UI helpers used during boot + error/info screens. Direct-
// to-display (no canvas) since these screens are static.
#pragma once
#include <Arduino.h>

namespace boot_ui {

void clear();
void header(const String& title, uint16_t color);
void centerText(const String& line, int y, uint16_t color);
void footer(const String& hint);

// Block until any key is pressed and released. Drains the keyboard
// state at entry so a key held over from the previous screen doesn't
// instantly skip the wait.
void waitForAnyKey();

// Render a halted state with a header + detail line + footer. Loops
// forever -- use for fatal errors that need a power-cycle.
void halt(const String& head, const String& detail);

} // namespace boot_ui
