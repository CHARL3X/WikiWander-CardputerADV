// SD-card mount + shared-config readers. Wikiwander reads WiFi creds
// from /Cardputer/wifi.txt -- shared with CHARL3X so a single setup
// works for both apps. If the file is missing, the user is asked to
// set it up via CHARL3X first.
#pragma once
#include <Arduino.h>
#include <vector>

namespace sdcfg {

struct WiFiCred { String ssid; String password; };

bool begin();                 // mount SD on FSPI (CS=12, SCK=40, MISO=39, MOSI=14)
void ensureDirs();            // create /Wikiwander/ if missing

std::vector<WiFiCred> loadWiFi();   // reads /Cardputer/wifi.txt

// --- on-device WiFi management (no PC needed) ---
// All three persist to /Cardputer/wifi.txt in the same alternating
// SSID/password format loadWiFi() reads, so a network added on the
// device shows up for both apps and survives reflashing the SD.

// Rewrite the whole file from `creds`. Creates /Cardputer/ if missing.
bool saveWiFi(const std::vector<WiFiCred>& creds);

// Upsert one network: if `ssid` already has an entry its password is
// replaced, otherwise the pair is appended. Returns false on IO error.
bool addWiFi(const String& ssid, const String& password);

// Drop the entry (if any) matching `ssid`. Returns false on IO error.
bool removeWiFi(const String& ssid);

// Stored password for `ssid`, or empty if not saved. Lets the WiFi
// picker reconnect to a known network without re-typing.
String passwordFor(const String& ssid);

} // namespace sdcfg
