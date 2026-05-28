# Wikiwander

A small pocket library for the M5Stack Cardputer ADV. Random Wikipedia
articles, today's events, search, save-to-SD library, walk-trail
backtracking — text-only, sepia-on-black, designed for the 240×135
screen of a pocket device.

Runs as a guest app under bmorcelli's
[Launcher](https://github.com/bmorcelli/Launcher) — drop the `.bin` on
the SD card, install via the launcher, and you're done.

## What you get

- **Random** — fresh article from Wikipedia's curated random pool,
  with a built-in filter that re-rolls past biographies (toggle with
  `p`)
- **Today's events** — Wikipedia's "On This Day" curated feed for
  the device's current date
- **Search** — live MediaWiki opensearch, tab between input and the
  results list
- **Saved library** — articles stored as `.md` files on SD with
  frontmatter (`/Wikiwander/articles/`)
- **Walk trail** — every random/where-next pushes onto an in-memory
  trail; `b` steps backward through your journey
- **QR share** — `q` in the article reader shows a scannable code of
  the canonical Wikipedia URL so you can continue reading on a phone
- **Audio feedback** — subtle ES8311 speaker blips for save/reroll/
  back/error/nav; mute with `m`
- **Battery + WiFi indicators** in the home status bar
- All rendered via `M5Canvas` full-screen sprites — no ST7789 tearing

## Install (the easy way)

1. Set up [bmorcelli's Launcher](https://github.com/bmorcelli/Launcher)
   on your Cardputer ADV (one-time)
2. Copy `dist/Wikiwander.bin` from a release onto the SD card's
   `/apps/` folder
3. From the launcher, install the .bin
4. Boot Wikiwander — first run will ask you to set up WiFi (see below)

## WiFi setup

Wikiwander reads credentials from `/Cardputer/wifi.txt` on the SD card,
sharing the convention used by [CharlieOS / CardputerLLM](https://github.com/CHARL3X/CardputerLLM).

If you don't already have that file, pop the SD card into a computer
and create it. Format is one SSID per line followed by its password,
with blank lines separating networks:

```
HomeNetwork
my-home-password

OfficeWiFi
my-office-password
```

Wikiwander scans nearby networks at boot and only attempts SSIDs that
are actually in range — multi-location creds are fine, they don't
slow each other down.

## Build from source

Requires PlatformIO. The project uses the stock `espressif32@6.12.0`
platform (no pioarduino fork).

```bash
cd Wikiwander
pio run -e cardputer        # builds dist/Wikiwander.bin
```

For the parser + transport tests on host (no device required), there's
a separate native test loop. The host build uses `cl.exe` directly via
the included `test/run_native.bat` (Windows + MSVC Build Tools), since
PlatformIO's native env doesn't ship a host C++ compiler:

```bat
cd Wikiwander
test\run_native.bat         :: 46 unit tests, ~2 seconds
test\run_walker.bat         :: live walker against Wikipedia, ~5 seconds
```

## Keyboard reference

### Home

| Key | Action |
|---|---|
| `r` | Random article |
| `t` | Today's events |
| `/` | Search |
| `s` | Saved library |
| `m` | Toggle audio mute |
| `p` | Toggle people-filter (default: filter on) |
| arrows | Move selection |
| enter | Open selected |
| del | Back to bmorcelli's launcher |

### Article reader

| Key | Action |
|---|---|
| arrows | Scroll line by line |
| `[` `]` | Page up / down |
| enter | Next page; at end, opens "what next?" picker |
| `r` | Instant reroll (skips the picker) |
| `s` | Save to library |
| `q` | Show QR code of article URL |
| `b` | Step back through walk trail (when history exists) |
| del | Back to home |

### What next? picker (after enter at end of article)

| Key | Action |
|---|---|
| `1` | Wander from this — find related articles |
| `2` | Random article |
| `3` | Today's events |
| arrows + enter | Pick highlighted |
| del | Back to article |

### Search

| Key | Action |
|---|---|
| typing | Add to query |
| del | Backspace |
| Tab | Switch focus between input and result list |
| arrows | Navigate results (in list focus) |
| `1`–`9` | Jump to result (in list focus) |
| enter | Open top result (input) / highlighted (list) |

## Architecture

```
lib/wiki/                 Pure C++, no Arduino/M5 dependencies
├── wiki_types.h          ArticleSummary, RelatedItem, SearchHit, TodayEvent
├── wiki_parser.{h,cpp}   JSON → structs (ArduinoJson 7 with filter for
│                         streamed parsing of the heavy onthisday feed)
├── transport.h           ByteReader interface + BufferReader impl + Transport
├── wiki_client.{h,cpp}   URL construction + parser glue

src/net/transport_device.cpp
                          WiFiClientSecure-backed Transport with
                          chunked-aware ByteReaders for streaming

src/storage/
├── settings.{h,cpp}      NVS (namespace: wikiwander)
├── sd_config.{h,cpp}     SD mount + wifi.txt loading
└── wiki_store.{h,cpp}    Article save/load/list as .md

src/ui/
├── colors.h              Sepia palette + layout constants
├── boot_ui.{h,cpp}       Direct-draw helpers for static screens
├── splash.{h,cpp}        Boot animation
├── sound.{h,cpp}         Audio feedback (mute toggle in NVS)
├── indicators.{h,cpp}    Battery + WiFi icons
├── text_utils.{h,cpp}    UTF-8→ASCII, word-wrap, ellipsize
└── wiki_screen.{h,cpp}   Screen state machine
```

## Credits

- [Wikipedia REST API](https://en.wikipedia.org/api/rest_v1/) — does
  all the actual work
- [ArduinoJson](https://arduinojson.org/) — streaming JSON parser
- [M5Stack](https://m5stack.com/) — the Cardputer ADV hardware
- [bmorcelli's Launcher](https://github.com/bmorcelli/Launcher) —
  the host shell

## License

MIT. See `LICENSE`.
