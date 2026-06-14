// Thin wrapper over the ESP32 WiFi radio for the on-device network
// picker. Scanning and connecting are exposed as poll-friendly
// primitives so the UI layer can keep its spinner animating during the
// (multi-second) blocking phases without this module knowing anything
// about the display.
//
// main.cpp keeps its own boot-time auto-connect loop (it intersects
// saved creds against a scan and animates a branded frame); this module
// serves the interactive "scan, pick, type password, connect" flow.
#pragma once
#include <Arduino.h>
#include <vector>

namespace wifimgr {

struct Net {
    String  ssid;
    int32_t rssi;   // dBm; higher (closer to 0) is stronger
    bool    open;   // true when the AP needs no password
};

// Kick an async scan. Pair with scanState()/collectScan().
void startScan();

// WiFi.scanComplete() passthrough: <0 means running (-1) or failed
// (-2); >=0 is the number of networks found.
int scanState();

// Read the finished scan into a de-duplicated, signal-sorted list
// (strongest first, hidden/blank SSIDs dropped) and free the scan
// buffer. Safe to call once scanState() >= 0.
std::vector<Net> collectScan();

// Begin a connection attempt (non-blocking). Poll isConnected().
void beginConnect(const String& ssid, const String& password);

bool isConnected();

// Tear down any in-flight scan and disconnect attempt. Used when the
// user backs out mid-scan so we don't leave the radio half-busy.
void cancel();

} // namespace wifimgr
