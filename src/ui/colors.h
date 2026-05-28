// Wikiwander palette + layout constants. Library / encyclopedia feel:
// warm sepia accent against a near-black field with cool dim greys.
#pragma once
#include <Arduino.h>

namespace ui {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kStatusH = 14;
constexpr int kHintH   = 20;
constexpr int kBodyY   = kStatusH;
constexpr int kBodyH   = kScreenH - kStatusH - kHintH;  // 101
constexpr int kPadX    = 4;

constexpr uint16_t kBg       = 0x0000;
constexpr uint16_t kPanel    = 0x0841;   // very dark cool-grey panel
constexpr uint16_t kDivider  = 0x2104;   // cool dark hairline
constexpr uint16_t kDim      = 0x6B4D;
constexpr uint16_t kIdle     = 0xEF7D;
constexpr uint16_t kAccent   = 0xBC60;   // sepia / aged paper (Wikiwander brand)
constexpr uint16_t kAccentLo = 0x7B00;   // dim sepia for outlines
constexpr uint16_t kHighlight= 0xFE40;   // brighter highlight for selected row
constexpr uint16_t kWarn     = 0xFD60;
constexpr uint16_t kErr      = 0xF884;

} // namespace ui
