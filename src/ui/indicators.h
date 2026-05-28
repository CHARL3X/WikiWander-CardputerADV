// Small status-bar widgets: battery state-of-charge bar + WiFi
// signal-strength bars. Both designed to live in the rightmost
// ~28 px of the 14-px-tall status bar.
#pragma once
#include <Arduino.h>
#include <M5GFX.h>

namespace ui {

// Draws battery + wifi at right side of status bar starting at
// `rightEdgeX` (the X coord of the rightmost pixel they should
// occupy). Returns the leftmost X used so callers can right-align
// labels next to them.
int drawIndicators(M5Canvas& c, int rightEdgeX, int statusY);

} // namespace ui
