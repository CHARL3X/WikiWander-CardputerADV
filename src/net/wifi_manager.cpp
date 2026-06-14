#include "wifi_manager.h"
#include <WiFi.h>
#include <algorithm>

namespace wifimgr {

void startScan() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(60);
    // async=true so the caller can animate while the ~2-3s scan runs;
    // show_hidden=false because we can't connect to a blank SSID from
    // the picker anyway.
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
}

int scanState() {
    return WiFi.scanComplete();
}

std::vector<Net> collectScan() {
    std::vector<Net> out;
    int n = WiFi.scanComplete();
    if (n < 0) { WiFi.scanDelete(); return out; }

    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;   // hidden -- can't pick it

        // De-dup: keep only the strongest sighting of each SSID (mesh
        // / multi-AP networks broadcast the same name several times).
        bool merged = false;
        for (auto& e : out) {
            if (e.ssid == ssid) {
                if (WiFi.RSSI(i) > e.rssi) e.rssi = WiFi.RSSI(i);
                merged = true;
                break;
            }
        }
        if (merged) continue;

        Net net;
        net.ssid = ssid;
        net.rssi = WiFi.RSSI(i);
        net.open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        out.push_back(net);
    }
    WiFi.scanDelete();

    std::sort(out.begin(), out.end(),
              [](const Net& a, const Net& b) { return a.rssi > b.rssi; });
    return out;
}

void beginConnect(const String& ssid, const String& password) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
}

bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void cancel() {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) WiFi.scanDelete();
}

} // namespace wifimgr
