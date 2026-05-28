#include "sd_config.h"
#include <SD.h>
#include <SPI.h>

namespace {

constexpr int kSdCs    = 12;
constexpr int kSdSck   = 40;
constexpr int kSdMiso  = 39;
constexpr int kSdMosi  = 14;

String trimWS(const String& s) {
    int a = 0, b = s.length() - 1;
    while (a <= b && isspace((uint8_t)s[a])) ++a;
    while (b >= a && isspace((uint8_t)s[b])) --b;
    return s.substring(a, b + 1);
}

String readFileWhole(const char* path) {
    if (!SD.exists(path)) return String();
    File f = SD.open(path, FILE_READ);
    if (!f) return String();
    String out;
    while (f.available()) out += (char)f.read();
    f.close();
    return out;
}

} // namespace

namespace sdcfg {

bool begin() {
    // Match CHARL3X SPI pinout exactly so both apps share the same
    // SD on the Cardputer ADV without contention.
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    return SD.begin(kSdCs, SPI, 25000000);
}

void ensureDirs() {
    if (!SD.exists("/Wikiwander"))         SD.mkdir("/Wikiwander");
    if (!SD.exists("/Wikiwander/articles")) SD.mkdir("/Wikiwander/articles");
}

std::vector<WiFiCred> loadWiFi() {
    // Same format as CHARL3X: alternating SSID + password lines,
    // blank lines / "# ..." comments ignored. Reading the same
    // /Cardputer/wifi.txt means a single setup works for both apps.
    std::vector<WiFiCred> out;
    String raw = readFileWhole("/Cardputer/wifi.txt");
    if (raw.length() == 0) return out;

    std::vector<String> lines;
    int start = 0;
    for (int i = 0; i <= (int)raw.length(); i++) {
        if (i == (int)raw.length() || raw[i] == '\n') {
            String line = raw.substring(start, i);
            line = trimWS(line);
            if (line.length() > 0 && line[0] != '#') lines.push_back(line);
            start = i + 1;
        }
    }
    for (size_t j = 0; j + 1 < lines.size(); j += 2) {
        out.push_back({lines[j], lines[j + 1]});
    }
    return out;
}

} // namespace sdcfg
