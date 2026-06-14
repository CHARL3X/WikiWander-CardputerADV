// Wikiwander -- standalone Cardputer ADV app. Boots, mounts SD,
// connects WiFi (reads /Cardputer/wifi.txt shared with CHARL3X),
// hands off to the wiki_screen state machine.
//
// Lives as a separate .bin under bmorcelli's Launcher -- not bundled
// with CHARL3X. See README for install steps.

#include <M5Cardputer.h>
#include <WiFi.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "storage/sd_config.h"
#include "storage/settings.h"
#include "storage/wiki_store.h"
#include "net/transport_device.h"
#include "ui/colors.h"
#include "ui/boot_ui.h"
#include "ui/splash.h"
#include "ui/sound.h"
#include "ui/wiki_screen.h"
#include "../lib/wiki/wiki_client.h"

// Override arduino-esp32's weak hook so the loopTask gets a stack
// big enough for WiFiClientSecure's TLS handshake. Same lesson as
// CHARL3X -- the default 8 KB is too tight.
uint32_t getArduinoLoopTaskStackSize(void) { return 24576; }

namespace {

constexpr const char* kUserAgent =
    "CHARL3X-Wikiwander/0.1 (github.com/CHARL3X/CardputerLLM)";

// Render the WiFi-connect screen into a canvas. Called repeatedly
// during the connect loop so the pulse animates and the SSID label
// updates per-attempt.
void drawConnectFrame(M5Canvas& c, const String& ssid, int attempt, int total,
                      uint32_t phaseMs) {
    using namespace ui;
    c.fillScreen(kBg);

    // Branded title at top
    c.setFont(&fonts::FreeSerifBoldItalic9pt7b);
    c.setTextSize(1);
    c.setTextColor(kAccent, kBg);
    const char* t = "Wikiwander";
    int tw = c.textWidth(t);
    c.setCursor((kScreenW - tw) / 2, 14);
    c.print(t);

    // Pulsing ring center
    int cx = kScreenW / 2;
    int cy = 70;
    float p = (phaseMs % 1200) / 1200.0f;
    int r1 = (int)(6 + p * 16);
    uint8_t fadeT = (uint8_t)(255 * (1.0f - p));
    uint16_t fade = M5Cardputer.Display.color565(
        (uint8_t)((0xBC * fadeT) / 255),
        (uint8_t)((0x60 * fadeT) / 255),
        0);
    c.drawCircle(cx, cy, r1, fade);
    c.fillCircle(cx, cy, 3, kAccent);

    // SSID + attempt counter
    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(kIdle, kBg);
    String ssidShort = ssid;
    while (c.textWidth(ssidShort.c_str()) > kScreenW - 16 && ssidShort.length() > 1) {
        ssidShort.remove(ssidShort.length() - 1);
    }
    if (ssidShort.length() != ssid.length()) ssidShort += "...";
    int sw = c.textWidth(ssidShort.c_str());
    c.setCursor((kScreenW - sw) / 2, 96);
    c.print(ssidShort);

    if (total > 1) {
        c.setFont(&fonts::Font0);
        c.setTextColor(kDim, kBg);
        char buf[16];
        snprintf(buf, sizeof(buf), "(%d/%d)", attempt, total);
        int bw = c.textWidth(buf);
        c.setCursor((kScreenW - bw) / 2, 114);
        c.print(buf);
    }
}

bool tryWiFiAnimated(M5Canvas* canv, const String& ssid, const String& pw,
                     uint32_t timeoutMs, int attempt, int total) {
    Serial.printf("[wifi] try '%s'\n", ssid.c_str());
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pw.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
        if (canv) {
            drawConnectFrame(*canv, ssid, attempt, total, millis());
            canv->pushSprite(0, 0);
        }
        delay(40);
    }
    return WiFi.status() == WL_CONNECTED;
}

// Scan nearby APs (async, so we can animate during the ~2-3 second
// blocking phase). Returns the list of visible SSIDs. Empty on
// timeout or scan failure -- callers should treat that as "scan
// inconclusive" and fall back rather than refusing to connect.
std::vector<String> scanVisibleNetworks(M5Canvas* canv) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(80);

    int started = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
    (void)started;
    uint32_t scanStart = millis();
    constexpr uint32_t kScanTimeoutMs = 8000;

    while (true) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            std::vector<String> out;
            out.reserve(n);
            for (int i = 0; i < n; ++i) out.push_back(WiFi.SSID(i));
            WiFi.scanDelete();
            return out;
        }
        if (millis() - scanStart > kScanTimeoutMs) {
            WiFi.scanDelete();
            return {};
        }
        if (canv) {
            drawConnectFrame(*canv, "scanning...", 0, 0, millis());
            canv->pushSprite(0, 0);
        }
        delay(40);
    }
}

bool connectWiFi(M5Canvas* canv) {
    auto creds = sdcfg::loadWiFi();
    if (creds.empty()) {
        Serial.println("[wifi] no creds in /Cardputer/wifi.txt");
        return false;
    }

    // Scan first so we only attempt SSIDs that are actually in range.
    // Without this, each non-present saved network burns 10s of
    // WiFi.begin() timeout before the next is tried -- a 30s+ boot
    // experience for users who carry creds for several locations.
    std::vector<String> visible = scanVisibleNetworks(canv);
    Serial.printf("[wifi] scan visible=%d saved=%d\n",
                  (int)visible.size(), (int)creds.size());

    // Intersect saved creds with visible SSIDs.
    std::vector<sdcfg::WiFiCred> candidates;
    for (auto& c : creds) {
        for (auto& v : visible) {
            if (c.ssid == v) { candidates.push_back(c); break; }
        }
    }

    // If the scan returned nothing at all, fall back to trying every
    // saved network -- a flaky scan shouldn't refuse to connect.
    // If the scan returned results but none match saved, trust it
    // (the user's networks really aren't nearby; brute-force would
    // just waste time).
    if (candidates.empty()) {
        if (visible.empty()) {
            Serial.println("[wifi] scan empty, trying all saved as fallback");
            candidates = creds;
        } else {
            Serial.println("[wifi] no saved networks are nearby; giving up");
            return false;
        }
    }

    Serial.printf("[wifi] %d candidates after scan filter\n",
                  (int)candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (tryWiFiAnimated(canv, candidates[i].ssid, candidates[i].password,
                            10000, (int)i + 1, (int)candidates.size())) {
            Serial.printf("[wifi] connected on '%s' ip=%s\n",
                          candidates[i].ssid.c_str(),
                          WiFi.localIP().toString().c_str());
            return true;
        }
        Serial.printf("[wifi] timeout on '%s'\n", candidates[i].ssid.c_str());
    }
    return false;
}

// One-time onboarding card. Shown when settings::welcomed() is false;
// sets it to true on dismissal so subsequent boots skip it.
void runOnboarding() {
    using namespace ui;
    M5Canvas canv(&M5Cardputer.Display);
    canv.setColorDepth(16);
    if (!canv.createSprite(kScreenW, kScreenH)) {
        // Fallback: skip onboarding rather than risk a partial render
        return;
    }
    canv.fillScreen(kBg);

    // Title
    canv.setFont(&fonts::FreeSerifBoldItalic9pt7b);
    canv.setTextSize(1);
    canv.setTextColor(kAccent, kBg);
    const char* title = "Wikiwander";
    int tw = canv.textWidth(title);
    canv.setCursor((kScreenW - tw) / 2, 10);
    canv.print(title);
    // Hairlines flank the title
    int yL = 19;
    canv.drawLine(20, yL, (kScreenW - tw)/2 - 6, yL, kAccentLo);
    canv.drawLine((kScreenW + tw)/2 + 6, yL, kScreenW - 20, yL, kAccentLo);

    // Tagline (smaller, dim)
    canv.setFont(&fonts::Font0);
    canv.setTextColor(kDim, kBg);
    const char* tag = "a small pocket library";
    int sw = canv.textWidth(tag);
    canv.setCursor((kScreenW - sw) / 2, 28);
    canv.print(tag);

    // Key cheatsheet -- tight 5-row layout. Picked the keys that
    // most need surfacing (people-filter, mute, QR are non-obvious;
    // the rest reinforce the home actions).
    canv.setFont(&fonts::Font0);
    canv.setTextSize(1);
    struct Row { const char* keys; const char* desc; };
    Row rows[] = {
        {"r",          "random article"},
        {"t",          "today's events"},
        {"/",          "search by name"},
        {"s",          "save to library"},
        {"enter",      "next page / what next"},
        {"q",          "QR share to phone"},
        {"c",          "settings (size, audio)"},
    };
    int rowY = 42;
    constexpr int kRowH = 10;
    for (auto& r : rows) {
        canv.setTextColor(kAccent, kBg);
        canv.setCursor(kPadX + 20, rowY);
        canv.print(r.keys);
        canv.setTextColor(kIdle, kBg);
        canv.setCursor(kPadX + 64, rowY);
        canv.print(r.desc);
        rowY += kRowH;
    }

    // Footer
    canv.setFont(&fonts::Font0);
    canv.setTextColor(kDim, kBg);
    const char* foot = "press any key to begin";
    int fw = canv.textWidth(foot);
    canv.setCursor((kScreenW - fw) / 2, kScreenH - 14);
    canv.print(foot);
    canv.drawLine(0, kScreenH - kHintH, kScreenW, kScreenH - kHintH, kDivider);

    canv.pushSprite(0, 0);
    canv.deleteSprite();

    boot_ui::waitForAnyKey();
    settings::setWelcomed(true);
}

} // namespace

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(ui::kBg);

    Serial.begin(115200);
    uint32_t serialDeadline = millis() + 2000;
    while (!Serial && millis() < serialDeadline) delay(10);
    Serial.println();
    Serial.println("[boot] Wikiwander");

    settings::begin();
    sound::begin();
    splash::run();

    // Mount SD. Required: WiFi creds live there, and saved articles
    // need a place to land. Halt with a clear message if no card.
    if (!sdcfg::begin()) {
        boot_ui::halt("sd: mount failed",
                      "insert sd card and reboot");
    }
    sdcfg::ensureDirs();

    // First-launch onboarding card -- shown once per device, gated
    // by NVS so power cycles after the first don't re-show it.
    if (!settings::welcomed()) {
        runOnboarding();
    }

    // Animated connect screen: pulsing ring + the SSID being tried.
    // Re-rendered every 40 ms during the WiFi.begin wait so the user
    // never sees a frozen-looking "connecting...".
    M5Canvas connectCanv(&M5Cardputer.Display);
    connectCanv.setColorDepth(16);
    bool connectCanvOk = connectCanv.createSprite(ui::kScreenW, ui::kScreenH);
    M5Canvas* connectPtr = connectCanvOk ? &connectCanv : nullptr;
    if (!connectWiFi(connectPtr)) {
        if (connectCanvOk) connectCanv.deleteSprite();

        // Auto-connect found nothing usable. Drop straight into the
        // on-device WiFi picker so the user can scan, choose a network,
        // and type the password right here -- no PC, no SD-card editing.
        // It loops internally until they connect or back out.
        bool connected = wiki_ui::runWifiSetup();

        if (!connected) {
            // The user left setup without connecting. Last-resort help
            // screen -- editing the SD card from a PC still works as a
            // fallback, but the on-device picker above is the main path.
            using namespace ui;
            M5Cardputer.Display.fillScreen(kBg);
            M5Cardputer.Display.setFont(&fonts::Font0);
            M5Cardputer.Display.setTextColor(kErr, kBg);
            M5Cardputer.Display.setCursor(kPadX, 4);
            M5Cardputer.Display.print("NO WIFI");
            M5Cardputer.Display.drawLine(0, kStatusH - 1, kScreenW, kStatusH - 1, kDivider);
            M5Cardputer.Display.setFont(&fonts::Font2);
            M5Cardputer.Display.setTextColor(kIdle, kBg);
            M5Cardputer.Display.setCursor(kPadX, kBodyY + 6);
            M5Cardputer.Display.print("Wikiwander needs WiFi.");
            M5Cardputer.Display.setTextColor(kDim, kBg);
            M5Cardputer.Display.setFont(&fonts::Font0);
            M5Cardputer.Display.setCursor(kPadX, kBodyY + 28);
            M5Cardputer.Display.print("Get in range of a network, then");
            M5Cardputer.Display.setCursor(kPadX, kBodyY + 40);
            M5Cardputer.Display.print("power cycle to set it up on-device.");
            int hy = kScreenH - kHintH;
            M5Cardputer.Display.drawLine(0, hy, kScreenW, hy, kDivider);
            M5Cardputer.Display.setTextColor(kDim, kBg);
            M5Cardputer.Display.setCursor(kPadX, hy + 6);
            M5Cardputer.Display.print("power cycle to retry");
            while (true) delay(1000);
        }
    } else {
        if (connectCanvOk) connectCanv.deleteSprite();
    }

    // Sync NTP so saved_utc timestamps are real-world times rather
    // than 1970 epoch garbage. Best-effort: skip if it doesn't
    // respond inside a few seconds.
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    uint32_t ntpStart = millis();
    while (time(nullptr) < 1577836800 && millis() - ntpStart < 4000) {
        delay(150);
    }

    // Hand off to the wiki UI. It loops until the user hits `\`` to
    // exit; on exit we just spin in a halt screen since there's
    // nowhere meaningful to go (bmorcelli's launcher would re-run
    // us only on power-cycle).
    static wiki::DeviceTransport transport;
    static wiki::WikiClient client(&transport, kUserAgent);

    wiki_ui::run(client);

    // User asked to exit. Hand control back to bmorcelli's launcher
    // by pointing otadata at his partition (subtype TEST in his
    // layout) then resetting. If we're running standalone with a
    // FACTORY partition, point there instead -- relaunches us, which
    // is fine for a single-OS-app install. Either way, no "bye"
    // screen: the device just bounces back to the launcher.
    const esp_partition_t* target = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_TEST, nullptr);
    if (!target) {
        target = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    }
    if (target) {
        esp_ota_set_boot_partition(target);
        Serial.printf("[exit] boot set to %s @ 0x%lx, restarting\n",
                      target->label, (long)target->address);
    } else {
        Serial.println("[exit] no test/factory partition; plain restart");
    }
    delay(120);
    esp_restart();
}

void loop() {
    // Everything runs from setup(). The wiki_ui state machine has its
    // own internal loop with delay()s -- nothing to do here.
    delay(1000);
}
