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

} // namespace sdcfg
