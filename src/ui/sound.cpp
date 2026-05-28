#include "sound.h"
#include <M5Cardputer.h>
#include <Preferences.h>

namespace sound {

namespace {
constexpr const char* kNs       = "wikisound";
constexpr const char* kMuteKey  = "muted";
Preferences prefs;
bool g_muted = false;

void play(float freq, uint32_t durMs) {
    if (g_muted) return;
    // M5Cardputer.Speaker.tone() is non-blocking and queues the
    // tone in the I2S task. Volume is set globally at begin().
    M5Cardputer.Speaker.tone(freq, durMs);
}

void seq(float f1, uint32_t d1, float f2, uint32_t d2) {
    if (g_muted) return;
    M5Cardputer.Speaker.tone(f1, d1);
    delay(d1 + 4);
    M5Cardputer.Speaker.tone(f2, d2);
}
} // namespace

void begin() {
    prefs.begin(kNs, false);
    g_muted = prefs.getBool(kMuteKey, false);
    // M5Cardputer.begin() already initialized Speaker. Set volume
    // modestly -- this is feedback, not media playback.
    M5Cardputer.Speaker.setVolume(80);
}

bool muted()           { return g_muted; }
void setMuted(bool m)  { g_muted = m; prefs.putBool(kMuteKey, m); }

// Sound design intent:
//   save    - rising two-note, reads as "stored"
//   reroll  - one mid blip, neutral / cycling
//   open    - low gentle blip, calmer landing
//   back    - falling blip, "going back"
//   error   - sustained low buzz, unmistakable
//   nav     - very short high tick, minimal
void save()   { seq(1000, 28, 1500, 32); }
void reroll() { play(720, 22); }
void open()   { play(540, 18); }
void back()   { play(420, 22); }
void error()  { play(220, 90); }
void nav()    { play(1400, 8); }

} // namespace sound
