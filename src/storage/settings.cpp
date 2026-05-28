#include "settings.h"
#include <Preferences.h>

namespace {
Preferences prefs;
constexpr const char* kNs           = "wikiwander";
constexpr const char* kWelcomedKey  = "welcomed";
constexpr const char* kLastPageKey  = "last_page";
constexpr const char* kLastTitleKey = "last_title";
constexpr const char* kPeopleKey    = "people";
constexpr const char* kTextSizeKey  = "textsize";
} // namespace

namespace settings {

void begin() { prefs.begin(kNs, false); }

bool welcomed()             { return prefs.getBool(kWelcomedKey, false); }
void setWelcomed(bool v)    { prefs.putBool(kWelcomedKey, v); }

String lastPageId()         { return prefs.getString(kLastPageKey, ""); }
void   setLastPageId(const String& v) { prefs.putString(kLastPageKey, v); }

String lastTitle()          { return prefs.getString(kLastTitleKey, ""); }
void   setLastTitle(const String& v) { prefs.putString(kLastTitleKey, v); }

bool peopleAllowed()         { return prefs.getBool(kPeopleKey, false); }
void setPeopleAllowed(bool v) { prefs.putBool(kPeopleKey, v); }

String textSize() {
    String v = prefs.getString(kTextSizeKey, "small");
    if (v != "small" && v != "medium" && v != "large") v = "small";
    return v;
}
void setTextSize(const String& v) {
    if (v == "small" || v == "medium" || v == "large") {
        prefs.putString(kTextSizeKey, v);
    }
}

} // namespace settings
