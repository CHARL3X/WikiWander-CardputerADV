// Shared text utilities for the screen layer. Word-wrap, ASCII
// sanitize, ellipsize. Independent of any specific screen so the
// paginator + search results + saved list can all share them.
#pragma once
#include <Arduino.h>
#include <M5GFX.h>
#include <vector>

namespace ui {

// Replace any byte > 0x7F with '?'. The Cardputer's bitmap fonts are
// 7-bit; non-ASCII reads as garbage box characters otherwise. Lossy
// but doesn't crash. Run on titles/descriptions before drawing.
String toAscii(const String& s);

// Word-wrap `s` into lines that each fit within `maxPxWidth` when
// rendered with `dev`'s current font/textSize. Newlines in `s` are
// honored as hard breaks. Lines containing only whitespace are kept
// to preserve paragraph spacing.
std::vector<String> wrap(LovyanGFX& dev, const String& s, int maxPxWidth);

// Shrink a string to fit `maxPxWidth`, appending an ellipsis when
// truncated. Single line.
String ellipsize(LovyanGFX& dev, const String& s, int maxPxWidth);

} // namespace ui
