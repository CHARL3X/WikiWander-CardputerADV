// Wikiwander NVS-backed settings. Separate namespace from CHARL3X so
// the two apps don't trample each other's keys.
#pragma once
#include <Arduino.h>

namespace settings {

void begin();

// Has the user seen the welcome/boot screen at least once?
bool welcomed();
void setWelcomed(bool v);

// Last page-id visited. Used by the home screen's "resume" affordance:
// when non-empty we offer the user a single-keypress jump back to the
// article they were reading before power-cycle.
String lastPageId();
void   setLastPageId(const String& v);

// Last article's display title -- kept alongside lastPageId so the
// home screen can show what's resumable without re-fetching.
String lastTitle();
void   setLastTitle(const String& v);

// When false, random fetches re-roll past biographical pages
// (Wikipedia's random pool is ~30% people). Default false because
// the user feedback ("felt very people heavy") confirms biographies
// dominate the experience without filtering.
bool peopleAllowed();
void setPeopleAllowed(bool v);

// Body-text size for the article reader. One of "small" / "medium"
// / "large". Default "small" (Font2 bitmap) matches the original
// design; medium/large swap in FreeSans vector fonts for better
// readability at the cost of fewer lines per page.
String textSize();
void   setTextSize(const String& v);

} // namespace settings
