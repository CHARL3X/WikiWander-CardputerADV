// Wikiwander UI -- entry point that owns the screen state machine.
// Single public function: run() drives the whole experience until
// the user holds esc to exit (back to bmorcelli's launcher).
#pragma once
#include "../../lib/wiki/wiki_client.h"

namespace wiki_ui {

void run(wiki::WikiClient& client);

// On-device WiFi setup: scan, pick a network, type the password, save
// + connect -- no PC required. Creates its own full-screen canvas, so
// it can be called at boot (before run()) when auto-connect fails.
// Returns true if the device is connected when the user leaves.
bool runWifiSetup();

} // namespace wiki_ui
