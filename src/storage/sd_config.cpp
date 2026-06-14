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

bool saveWiFi(const std::vector<WiFiCred>& creds) {
    // /Cardputer is CHARL3X's dir -- it may not exist yet on a card
    // that only ever held Wikiwander, so create it before writing.
    if (!SD.exists("/Cardputer")) SD.mkdir("/Cardputer");

    // Write to a temp file then rename, so a power loss mid-write can't
    // leave a half-written wifi.txt that orphans every saved network.
    const char* tmp = "/Cardputer/wifi.tmp";
    SD.remove(tmp);
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) return false;
    f.println("# Wikiwander WiFi -- managed on-device (Settings > wifi).");
    f.println("# Lines alternate SSID then password; blank line separates.");
    for (const auto& c : creds) {
        if (c.ssid.length() == 0) continue;
        f.println(c.ssid);
        f.println(c.password);
        f.println();
    }
    f.close();

    SD.remove("/Cardputer/wifi.txt");
    return SD.rename(tmp, "/Cardputer/wifi.txt");
}

bool addWiFi(const String& ssid, const String& password) {
    if (ssid.length() == 0) return false;
    std::vector<WiFiCred> creds = loadWiFi();
    bool replaced = false;
    for (auto& c : creds) {
        if (c.ssid == ssid) { c.password = password; replaced = true; break; }
    }
    if (!replaced) creds.push_back({ssid, password});
    return saveWiFi(creds);
}

bool removeWiFi(const String& ssid) {
    std::vector<WiFiCred> creds = loadWiFi();
    std::vector<WiFiCred> kept;
    kept.reserve(creds.size());
    for (auto& c : creds) {
        if (c.ssid != ssid) kept.push_back(c);
    }
    return saveWiFi(kept);
}

String passwordFor(const String& ssid) {
    for (auto& c : loadWiFi()) {
        if (c.ssid == ssid) return c.password;
    }
    return String();
}

} // namespace sdcfg
