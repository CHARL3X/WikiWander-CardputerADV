// Tiny audio feedback layer over the Cardputer ADV's ES8311 speaker.
// All blips are sub-100ms, intentionally quiet -- not music, just
// tactile confirmation that an action registered. Mute state is
// persisted to NVS so user preference survives power cycles.
#pragma once

namespace sound {

void begin();
bool muted();
void setMuted(bool m);

// One-shot blips. Each is a single tone or a two-note flourish.
void save();     // rising two-note   (action confirmed, "filed")
void reroll();   // subtle mid blip   (forward toggle)
void open();     // gentle low blip   (article landed)
void back();     // descending blip   (stepped back / closed)
void error();    // low buzz          (something failed)
void nav();      // very short tick   (cursor moved)

} // namespace sound
