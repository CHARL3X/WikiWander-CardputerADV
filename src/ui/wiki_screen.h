// Wikiwander UI -- entry point that owns the screen state machine.
// Single public function: run() drives the whole experience until
// the user holds esc to exit (back to bmorcelli's launcher).
#pragma once
#include "../../lib/wiki/wiki_client.h"

namespace wiki_ui {

void run(wiki::WikiClient& client);

} // namespace wiki_ui
