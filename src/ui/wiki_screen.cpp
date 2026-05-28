// Wikiwander UI orchestrator. Five screens, one state machine, one
// full-screen canvas. Every screen draws into the canvas and pushes
// once -- never fillScreen on the bare ST7789.
//
// Screens:
//   home     three actions (random, search, saved)
//   loading  pulsing label while a network/IO op runs
//   article  paginated reader for a fetched/loaded ArticleSummary
//   related  "where next" picker built from morelike results
//   search   text input + results list
//   saved    saved-articles browser

#include "wiki_screen.h"
#include "colors.h"
#include "text_utils.h"
#include "sound.h"
#include "indicators.h"
#include "../storage/wiki_store.h"
#include "../storage/settings.h"

#include <M5Cardputer.h>
#include <M5GFX.h>
#include <vector>
#include <time.h>

namespace wiki_ui {

using namespace ui;

namespace {

// ---------- biographical-page sniff ----------

// Wikipedia's article pool is ~30% biographies. The `description`
// field is a stable one-line subtitle with consistent patterns for
// people: parenthetical date ranges, "(born YYYY)" / "(d. YYYY)",
// and profession keywords. Substring checking on these handles
// 80%+ of bios without needing structured data.
//
// Conservative-toward-filtering: we'd rather over-filter a few
// non-people than rotate through endless soccer players.
bool looksLikePerson(const std::string& desc) {
    if (desc.empty()) return false;
    std::string d = desc;
    for (auto& c : d) if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';

    // Strong signals -- explicit biographical markers.
    if (d.find("(born ") != std::string::npos) return true;
    if (d.find("(b. ") != std::string::npos)   return true;
    if (d.find("(d. ") != std::string::npos)   return true;

    // Year-range parenthetical: "(1834-1923)" or with en-dash.
    // We scan each "(...)" group for a 4-digit numeric prefix --
    // catches most life-span patterns without false-matching things
    // like "(1956 invention)" (which also reads as a person-ish hit,
    // acceptable noise).
    for (size_t i = 0; i < d.size(); ) {
        size_t open = d.find('(', i);
        if (open == std::string::npos) break;
        size_t close = d.find(')', open);
        if (close == std::string::npos) break;
        if (close - open >= 5) {
            bool startsYear = true;
            for (size_t k = 1; k <= 4; ++k) {
                char c = d[open + k];
                if (c < '0' || c > '9') { startsYear = false; break; }
            }
            if (startsYear) return true;
        }
        i = close + 1;
    }

    // Profession-keyword markers. Leading space avoids mid-word
    // matches like "manufacturer" catching "factor". List leans
    // toward common nationality-prefix patterns Wikipedia uses.
    static const char* markers[] = {
        " actor", " actress", " activist", " admiral", " architect",
        " astronaut", " astronomer", " athlete", " baseball player",
        " basketball player", " bishop", " boxer", " cardinal",
        " chemist", " chess player", " cinematographer", " comedian",
        " composer", " conductor", " cricketer", " cyclist",
        " dancer", " designer", " director", " drummer",
        " economist", " editor", " emperor", " engineer",
        " entertainer", " explorer", " field hockey player",
        " filmmaker", " footballer", " general", " geologist",
        " golfer", " guitarist", " gymnast", " historian",
        " ice hockey player", " illustrator", " journalist",
        " judge", " king", " lawyer", " linguist",
        " martial artist", " mathematician", " mayor", " missionary",
        " model", " monk", " musician", " novelist",
        " organist", " painter", " philosopher", " photographer",
        " physician", " physicist", " pianist", " poet",
        " politician", " president", " priest", " prince",
        " producer", " professor", " queen", " rabbi",
        " rapper", " referee", " revolutionary", " rower",
        " rugby player", " sailor", " saint", " scientist",
        " sculptor", " senator", " singer", " skater",
        " skier", " soccer player", " sociologist", " soldier",
        " songwriter", " statesman", " surgeon", " swimmer",
        " teacher", " tennis player", " theologian", " violinist",
        " volleyball player", " weightlifter", " wrestler",
        " writer", " yogi",
    };
    for (auto m : markers) {
        if (d.find(m) != std::string::npos) return true;
    }
    return false;
}

// ---------- friendly error formatter ----------

// WikiClient::lastError() returns technical strings like
// "transport: connect failed: en.wikipedia.org" or "http 503". Those
// are great in the Serial log but useless on a 240-px screen for a
// user who just wants to know what to try next. Map to short
// human-readable phrases; show the technical line below.
const char* prettyHead(const String& detail) {
    if (detail.startsWith("transport:"))  return "no internet";
    if (detail.startsWith("http 5"))      return "wikipedia is unhappy";
    if (detail.startsWith("http 4"))      return "article gone";
    if (detail.startsWith("parse"))       return "weird response";
    return "something went wrong";
}

// ---------- shared canvas ----------

struct Canvas {
    M5Canvas c{&M5Cardputer.Display};
    bool ok = false;
    Canvas() {
        c.setColorDepth(16);
        ok = c.createSprite(kScreenW, kScreenH);
        if (ok) {
            c.setFont(&fonts::Font2);
            c.setTextSize(1);
        }
    }
    ~Canvas() { if (ok) c.deleteSprite(); }
};

// ---------- key handling ----------

struct KeyEdge {
    std::vector<char> word;
    bool enter = false;
    bool del   = false;
    bool tab   = false;
    bool esc   = false;
    bool fn    = false;
    bool ctrl  = false;
    bool shift = false;
};

// Prime the edge-detect state with whatever keys are currently down.
// MUST be called once before each screen's main loop. Without this,
// any keypress that brought the user from one screen to the next
// (e.g. holding `r` to enter a random article) is treated as a fresh
// edge on the first iteration and re-fires immediately, kicking off
// an infinite-trigger loop. Lesson learned the hard way in
// CHARL3X/record_screen -- never enter a key-driven loop without
// priming.
void primeKeyEdge(std::vector<char>& prevWord,
                  bool& prevEnter, bool& prevDel,
                  bool& prevTab, bool& prevEsc) {
    M5Cardputer.update();
    auto& s = M5Cardputer.Keyboard.keysState();
    prevWord  = s.word;
    prevEnter = s.enter;
    prevDel   = s.del;
    prevTab   = s.tab;
    prevEsc   = false;
    for (char c : s.word) if (c == '`') { prevEsc = true; break; }
}

KeyEdge readKeysEdge(bool& prevAny, std::vector<char>& prevWord,
                     bool& prevEnter, bool& prevDel,
                     bool& prevTab,  bool& prevEsc) {
    KeyEdge out;
    M5Cardputer.update();
    auto& s = M5Cardputer.Keyboard.keysState();
    // detect new characters (in s.word but not in prevWord)
    for (char c : s.word) {
        bool was = false;
        for (char p : prevWord) if (p == c) { was = true; break; }
        if (!was) out.word.push_back(c);
    }
    out.enter = s.enter && !prevEnter;
    out.del   = s.del   && !prevDel;
    out.tab   = s.tab   && !prevTab;
    out.fn    = s.fn;
    out.ctrl  = s.ctrl;
    out.shift = s.shift;
    // Cardputer "esc" isn't surfaced directly -- treat backtick/grave
    // (\` produced via Fn + 0x60 or pressing the backtick key) as our
    // back/esc key. Many ports use this convention.
    bool escNow = false;
    for (char c : s.word) if (c == '`') { escNow = true; break; }
    out.esc = escNow && !prevEsc;
    prevEsc = escNow;

    prevAny   = s.enter || s.del || !s.word.empty() || s.tab;
    prevWord  = s.word;
    prevEnter = s.enter;
    prevDel   = s.del;
    prevTab   = s.tab;
    return out;
}

// ---------- common chrome ----------

void drawStatusBar(M5Canvas& c, const char* leftLabel,
                   const char* rightLabel = nullptr) {
    c.fillRect(0, 0, kScreenW, kStatusH, kBg);
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(kAccent, kBg);
    c.setCursor(kPadX, 4);
    c.print(leftLabel);
    if (rightLabel) {
        c.setTextColor(kDim, kBg);
        int rw = c.textWidth(rightLabel);
        c.setCursor(kScreenW - rw - kPadX, 4);
        c.print(rightLabel);
    }
    c.drawLine(0, kStatusH - 1, kScreenW, kStatusH - 1, kDivider);
}

// drawHintBar supports up to two Font0 rows in the 20-px footer. Top
// row carries the most context-specific hint (often with a right-side
// page-counter / state badge). Bottom row carries the always-true
// shortcuts. Pass nullptr for line2 to fall back to centered-vertical
// single-line layout.
void drawHintBar(M5Canvas& c, const char* left,
                 const char* right = nullptr,
                 const char* line2 = nullptr) {
    int hy = kScreenH - kHintH;
    c.fillRect(0, hy, kScreenW, kHintH, kBg);
    c.drawLine(0, hy, kScreenW, hy, kDivider);
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(kDim, kBg);
    int y1 = line2 ? hy + 3 : hy + 6;
    c.setCursor(kPadX, y1);
    c.print(left);
    if (right) {
        int rw = c.textWidth(right);
        c.setCursor(kScreenW - rw - kPadX, y1);
        c.print(right);
    }
    if (line2) {
        c.setCursor(kPadX, hy + 12);
        c.print(line2);
    }
}

// ---------- loading screen ----------

void drawLoading(M5Canvas& c, const char* label, uint32_t phaseMs) {
    c.fillScreen(kBg);
    drawStatusBar(c, "WIKIWANDER", "fetching...");
    drawHintBar(c, "");

    int cx = kScreenW / 2;
    int cy = kBodyY + kBodyH / 2 - 6;

    // 8-dot rotating spinner. Each dot's brightness depends on how
    // far behind the "leading" position it is, so the eye reads a
    // comet trail rotating clockwise. Cosf/sinf would work but a
    // precomputed unit circle keeps the loop trig-free.
    static const int8_t kDotsX[8] = {  14,  10,   0, -10, -14, -10,   0,  10};
    static const int8_t kDotsY[8] = {   0,  10,  14,  10,   0, -10, -14, -10};
    constexpr uint32_t kStepMs = 90;     // ~11 fps rotation
    int head = (int)((phaseMs / kStepMs) % 8);

    for (int i = 0; i < 8; ++i) {
        // distFromHead: how far behind the leading dot. 0 = head.
        int distFromHead = (head - i + 8) % 8;
        // Brightness falls off quadratically so the head pops.
        float bright = 1.0f - (distFromHead / 8.0f);
        bright *= bright;
        uint8_t r = (uint8_t)(0xBC * bright);
        uint8_t g = (uint8_t)(0x60 * bright);
        uint16_t col = M5Cardputer.Display.color565(r, g, 0);
        c.fillCircle(cx + kDotsX[i], cy + kDotsY[i], 2, col);
    }

    c.setFont(&fonts::Font2);
    c.setTextSize(1);
    c.setTextColor(kIdle, kBg);
    int tw = c.textWidth(label);
    c.setCursor((kScreenW - tw) / 2, cy + 26);
    c.print(label);
}

// Render a single loading frame.
void pushLoading(Canvas& canv, const char* label) {
    if (!canv.ok) return;
    drawLoading(canv.c, label, millis());
    canv.c.pushSprite(0, 0);
}

// ---------- home ----------

struct HomeAction {
    char        key;
    const char* label;
    const char* hint;
};

const HomeAction kHome[] = {
    {'r', "random",   "fetch a random article"},
    {'t', "today",    "what happened on this date"},
    {'/', "search",   "find an article by name"},
    {'s', "saved",    "browse saved articles"},
};
constexpr int kHomeCount = sizeof(kHome) / sizeof(kHome[0]);

enum class HomeChoice { None, Random, Today, Search, Saved, Settings, Resume, Exit };

HomeChoice runHome(Canvas& canv) {
    if (!canv.ok) return HomeChoice::Exit;
    int selected = 0;
    bool dirty = true;

    std::vector<char> prevWord;
    bool prevEnter = false, prevDel = false, prevTab = false, prevEsc = false, prevAny = false;
    primeKeyEdge(prevWord, prevEnter, prevDel, prevTab, prevEsc);

    int savedCount = (int)wiki_store::list().size();

    while (true) {
        if (dirty) {
            auto& c = canv.c;
            c.fillScreen(kBg);
            // Home status bar carries the only place we surface
            // battery + wifi indicators; other screens lean on the
            // right side of the bar for task-specific context (page
            // counts, hit counts, etc.) and stay quieter.
            c.fillRect(0, 0, kScreenW, kStatusH, kBg);
            c.setFont(&fonts::Font0);
            c.setTextSize(1);
            c.setTextColor(kAccent, kBg);
            c.setCursor(kPadX, 4);
            c.print("WIKIWANDER");
            int rightUsedAt = drawIndicators(c, kScreenW - kPadX, 3);
            // Tiny single-letter badges for non-default settings.
            // M = muted (audio off). P = people allowed (default is
            // off, so the badge shows when the user has opted into
            // biographies again).
            if (sound::muted()) {
                c.setTextColor(kDim, kBg);
                c.setCursor(rightUsedAt - 8, 4);
                c.print("M");
                rightUsedAt -= 10;
            }
            if (settings::peopleAllowed()) {
                c.setTextColor(kAccent, kBg);
                c.setCursor(rightUsedAt - 8, 4);
                c.print("P");
            }
            c.drawLine(0, kStatusH - 1, kScreenW, kStatusH - 1, kDivider);

            // Title block: "where to?" centered + date subtitle
            // anchors the user in time and surfaces that "today"
            // pulls from today's wiki feed.
            c.setFont(&fonts::FreeSerifBoldItalic9pt7b);
            c.setTextSize(1);
            c.setTextColor(kAccent, kBg);
            const char* t = "where to?";
            int tw = c.textWidth(t);
            c.setCursor((kScreenW - tw) / 2, kBodyY + 4);
            c.print(t);

            // Date underneath, smaller + dim. Format: "Wed . May 27"
            time_t now = time(nullptr);
            struct tm tmnow;
            localtime_r(&now, &tmnow);
            static const char* kDay[] =
                {"sun","mon","tue","wed","thu","fri","sat"};
            static const char* kMon[] =
                {"jan","feb","mar","apr","may","jun",
                 "jul","aug","sep","oct","nov","dec"};
            char dateBuf[24];
            if (now > 1577836800) {
                snprintf(dateBuf, sizeof(dateBuf), "%s . %s %d",
                         kDay[tmnow.tm_wday],
                         kMon[tmnow.tm_mon],
                         tmnow.tm_mday);
            } else {
                // NTP not synced yet -- gentle "..." so we don't
                // surface a 1970 date that looks broken.
                snprintf(dateBuf, sizeof(dateBuf), "...");
            }
            c.setFont(&fonts::Font0);
            c.setTextColor(kDim, kBg);

            // Resume affordance: when lastPageId is set, show the
            // last-read title right-aligned on the date line with a
            // [u] hotkey marker. Keeps both date + resume cue on
            // the same row -- cleaner than stacking lines.
            String resumeTitle = settings::lastTitle();
            String resumePid   = settings::lastPageId();
            bool   canResume   = (resumePid.length() > 0 &&
                                  resumeTitle.length() > 0);
            if (canResume) {
                // Date on left
                c.setCursor(kPadX, kBodyY + 18);
                c.print(dateBuf);
                // Resume marker on right
                String resumeLabel = String("[u] ") +
                                     toAscii(resumeTitle);
                int dateW = c.textWidth(dateBuf);
                int avail = kScreenW - 2 * kPadX - dateW - 8;
                String shown = ellipsize(c, resumeLabel, avail);
                c.setTextColor(kAccent, kBg);
                int rw = c.textWidth(shown.c_str());
                c.setCursor(kScreenW - kPadX - rw, kBodyY + 18);
                c.print(shown);
            } else {
                int dw = c.textWidth(dateBuf);
                c.setCursor((kScreenW - dw) / 2, kBodyY + 18);
                c.print(dateBuf);
            }

            // Action rows. 4 rows in the 101-px body region after the
            // title block leaves ~70 px. 16 px per row with 1 px gap
            // fits cleanly without crowding.
            c.setFont(&fonts::Font2);
            int rowY = kBodyY + 28;
            const int rowH = 17;
            for (int i = 0; i < kHomeCount; ++i) {
                bool sel = (i == selected);
                int y = rowY + i * rowH;
                if (sel) {
                    c.fillRoundRect(kPadX + 4, y - 1,
                                    kScreenW - 2 * (kPadX + 4), 16, 3, kAccentLo);
                }
                char keyStr[6];
                snprintf(keyStr, sizeof(keyStr), "[%c]", kHome[i].key);
                c.setTextColor(sel ? kBg : kAccent, sel ? kAccentLo : kBg);
                c.setCursor(kPadX + 10, y + 1);
                c.print(keyStr);
                c.setTextColor(sel ? kBg : kIdle, sel ? kAccentLo : kBg);
                c.setCursor(kPadX + 40, y + 1);
                c.print(kHome[i].label);
                // Saved-count badge on the saved row
                if (kHome[i].key == 's') {
                    char buf[12];
                    snprintf(buf, sizeof(buf), "(%d)", savedCount);
                    int bw = c.textWidth(buf);
                    c.setTextColor(sel ? kBg : kDim, sel ? kAccentLo : kBg);
                    c.setCursor(kScreenW - kPadX - 10 - bw, y + 1);
                    c.print(buf);
                }
            }

            bool canResumeHint = (settings::lastPageId().length() > 0 &&
                                  settings::lastTitle().length() > 0);
            drawHintBar(c,
                        "r/t//s/enter pick . arrows nav",
                        nullptr,
                        canResumeHint
                            ? "u resume . c settings . m mute"
                            : "c settings . m mute . p people");
            c.pushSprite(0, 0);
            dirty = false;
        }

        auto k = readKeysEdge(prevAny, prevWord, prevEnter, prevDel, prevTab, prevEsc);
        for (char c : k.word) {
            if (c == ';' || c == ',') { sound::nav(); selected = (selected + kHomeCount - 1) % kHomeCount; dirty = true; }
            else if (c == '.') {
                sound::nav();
                selected = (selected + 1) % kHomeCount; dirty = true;
            }
            else if (c == '/') return HomeChoice::Search;
            else if (c == 'r' || c == 'R') return HomeChoice::Random;
            else if (c == 's' || c == 'S') return HomeChoice::Saved;
            else if (c == 't' || c == 'T') return HomeChoice::Today;
            else if (c == 'c' || c == 'C') return HomeChoice::Settings;
            else if ((c == 'u' || c == 'U') &&
                     settings::lastPageId().length() > 0) {
                return HomeChoice::Resume;
            }
            else if (c == 'm' || c == 'M') {
                // Toggle audio feedback. Re-render to update the
                // tiny "M" badge in the status bar.
                sound::setMuted(!sound::muted());
                if (!sound::muted()) sound::nav();
                dirty = true;
            }
            else if (c == 'p' || c == 'P') {
                // Toggle whether random fetches include biographies.
                // Re-render to update the "P" badge.
                settings::setPeopleAllowed(!settings::peopleAllowed());
                sound::nav();
                dirty = true;
            }
        }
        if (k.enter) {
            switch (selected) {
                case 0: return HomeChoice::Random;
                case 1: return HomeChoice::Today;
                case 2: return HomeChoice::Search;
                case 3: return HomeChoice::Saved;
            }
        }
        // Both backtick (`) and del act as back/exit.
        if (k.esc || k.del) { sound::back(); return HomeChoice::Exit; }
        delay(20);
    }
}

// ---------- article reader ----------

enum class ArticleAction { Back, Related, Save, Random, TrailBack };

// Forward: defined a bit below so the article reader can summon the
// QR share overlay without changing its own return type.
void showQrShare(Canvas& canv, const wiki::ArticleSummary& a);
void runSettings(Canvas& canv);

// Paginated reader. trailDepth is how many articles deep into the
// walk we are; when > 0 the 'b' key surfaces TrailBack so the user
// can step back through their journey one article at a time.
//
// followPageIdOut: set by this function when the user picks an inline
// link with enter. Caller (walk loop) interprets a non-empty value
// as "skip the related picker, fetch this pageId directly". Cleared
// by this function at entry.
ArticleAction runArticle(Canvas& canv, const wiki::ArticleSummary& a,
                         bool isSaved, int trailDepth,
                         String& followPageIdOut) {
    followPageIdOut = "";
    if (!canv.ok) return ArticleAction::Back;
    auto& c = canv.c;

    String asciiTitle = toAscii(String(a.title.c_str()));
    String asciiDesc  = toAscii(String(a.description.c_str()));

    // Reserve top region for title (1-2 lines) + description (1 line)
    // + spacing. The remaining body is paginated.
    c.setFont(&fonts::FreeSerifBoldItalic9pt7b);
    auto titleLines = wrap(c, asciiTitle, kScreenW - 2 * kPadX - 4);
    if (titleLines.size() > 2) titleLines.resize(2);
    int titleH = (int)titleLines.size() * 14;

    // Compose a meta line: "~N min" reading estimate + link count
    // (right-aligned later, drawn dim). Reading rate ~200 wpm; count
    // words by counting transitions from space to non-space, which
    // is cheap and good-enough.
    auto countWords = [](const std::string& s) {
        int n = 0;
        bool inWord = false;
        for (char ch : s) {
            bool space = (ch == ' ' || ch == '\n' || ch == '\t');
            if (!space && !inWord) ++n;
            inWord = !space;
        }
        return n;
    };
    int wordCount = countWords(a.extract);
    int readSec = (wordCount * 60 + 100) / 200;
    String metaText;
    if (readSec >= 30) {
        if (readSec < 60) metaText = String(readSec) + "s read";
        else              metaText = "~" + String((readSec + 30) / 60) + " min";
    }

    c.setFont(&fonts::Font0);
    String descLine = ellipsize(c, asciiDesc,
                                kScreenW - 2 * kPadX - 4
                                - (metaText.length() ? c.textWidth(metaText.c_str()) + 8 : 0));
    int descH = (asciiDesc.length() > 0 || metaText.length() > 0) ? 12 : 0;

    int headerH = titleH + descH + 4;
    int readY  = kBodyY + headerH;
    int readH  = kBodyH - headerH;

    // Pick body font per user setting. Small = Font2 bitmap (the
    // original); Medium/Large swap in FreeSans vector fonts for
    // smoother readability at the cost of fewer lines per page.
    // The font drives both wrap measurement and rendering, so it
    // must be set BEFORE wrap is called.
    const lgfx::IFont* bodyFont = &fonts::Font2;
    int bodyLineH = 14;
    {
        String sz = settings::textSize();
        if (sz == "large") {
            bodyFont = &fonts::FreeSans12pt7b;
            bodyLineH = 19;
        } else if (sz == "medium") {
            bodyFont = &fonts::FreeSans9pt7b;
            bodyLineH = 15;
        }
    }

    // Two parallel views of the body: token-wrapped (links + text)
    // and plain-wrapped fallback. Token wrap is used iff the server
    // returned extract_html (live fetches do, saved-from-disk
    // articles don't). The fallback keeps saved articles working.
    c.setFont(bodyFont);
    bool useTokenView = !a.extractHtml.empty();
    std::vector<wiki::ExtractToken> tokens;
    std::vector<std::vector<RenderedSegment>> tokenLines;
    std::vector<LinkInfo> links;
    std::vector<String> plainLines;
    int totalLines = 0;
    if (useTokenView) {
        tokens = wiki::tokenizeExtractHtml(a.extractHtml);
        tokenLines = wrapTokens(c, tokens, kScreenW - 2 * kPadX, links);
        totalLines = (int)tokenLines.size();
    } else {
        String asciiBody = toAscii(String(a.extract.c_str()));
        plainLines = wrap(c, asciiBody, kScreenW - 2 * kPadX);
        totalLines = (int)plainLines.size();
    }
    const int kLineH = bodyLineH;
    int linesPerPage = readH / kLineH;
    if (linesPerPage < 1) linesPerPage = 1;
    int scrollLine = 0;

    // Link nav state: -1 means "no link selected". Tab cycles
    // through the links list, auto-scrolling to keep the chosen
    // link on screen. Enter on a selected link follows it.
    int selectedLink = -1;

    bool dirty = true;
    bool saved = isSaved;

    std::vector<char> prevWord;
    bool prevEnter = false, prevDel = false, prevTab = false, prevEsc = false, prevAny = false;
    primeKeyEdge(prevWord, prevEnter, prevDel, prevTab, prevEsc);

    while (true) {
        if (dirty) {
            c.fillScreen(kBg);
            // Status
            int curPage = scrollLine / linesPerPage + 1;
            int totalPages = (totalLines + linesPerPage - 1) / linesPerPage;
            if (totalPages < 1) totalPages = 1;
            // Status right side: save badge + trail-depth chevron +
            // page or link counter. When a link is selected we
            // surface its position (N/M) so the user knows roughly
            // where they are in the link list; otherwise the page
            // counter as before.
            char rightBuf[32];
            if (useTokenView && selectedLink >= 0) {
                snprintf(rightBuf, sizeof(rightBuf), "%slink %d/%d",
                         saved ? "* " : "",
                         selectedLink + 1, (int)links.size());
            } else if (trailDepth > 0) {
                snprintf(rightBuf, sizeof(rightBuf), "%s<%d  %d/%d",
                         saved ? "* " : "", trailDepth, curPage, totalPages);
            } else {
                snprintf(rightBuf, sizeof(rightBuf), "%s%d/%d",
                         saved ? "* " : "", curPage, totalPages);
            }
            drawStatusBar(c, "WIKI", rightBuf);

            // Title (serif italic, sepia)
            c.setFont(&fonts::FreeSerifBoldItalic9pt7b);
            c.setTextSize(1);
            c.setTextColor(kAccent, kBg);
            for (size_t i = 0; i < titleLines.size(); ++i) {
                c.setCursor(kPadX, kBodyY + 2 + (int)i * 14);
                c.print(titleLines[i]);
            }
            // Description + meta row: description left-aligned in
            // dim grey, reading-time / link count right-aligned in
            // a slightly warmer dim sepia so the eye treats it as
            // a separate annotation rather than article subtitle.
            if (descH > 0) {
                c.setFont(&fonts::Font0);
                int rowY = kBodyY + titleH + 2;
                if (descLine.length() > 0) {
                    c.setTextColor(kDim, kBg);
                    c.setCursor(kPadX, rowY);
                    c.print(descLine);
                }
                // Compose right-side meta: reading time, maybe link count
                String rightMeta = metaText;
                if (useTokenView && !links.empty()) {
                    if (rightMeta.length() > 0) rightMeta += " . ";
                    rightMeta += String(links.size()) + " links";
                }
                if (rightMeta.length() > 0) {
                    c.setTextColor(kAccentLo, kBg);
                    int rmw = c.textWidth(rightMeta.c_str());
                    c.setCursor(kScreenW - rmw - kPadX, rowY);
                    c.print(rightMeta);
                }
            }
            // Body lines (paginated). Token view renders each
            // segment with per-style color (sepia for links, idle
            // for plain text). Font is whatever the textSize
            // setting resolved to above.
            c.setFont(bodyFont);
            for (int i = 0; i < linesPerPage; ++i) {
                int idx = scrollLine + i;
                if (idx >= totalLines) break;
                int y = readY + i * kLineH;
                if (useTokenView) {
                    int x = kPadX;
                    for (auto& seg : tokenLines[idx]) {
                        bool isLink = (seg.linkIndex >= 0);
                        bool isSel  = (seg.linkIndex == selectedLink);
                        int segW = c.textWidth(seg.text.c_str());
                        if (isSel) {
                            // Selected link: brighter highlight color
                            // (kHighlight is a more luminous warm tone
                            // than the standard sepia) + an underline.
                            // Lighter than the old inverted-background
                            // treatment -- reads as "active" without
                            // visually shouting.
                            c.setTextColor(kHighlight, kBg);
                            c.setCursor(x, y);
                            c.print(seg.text);
                            c.drawLine(x, y + kLineH - 2,
                                       x + segW - 1, y + kLineH - 2,
                                       kHighlight);
                        } else if (isLink) {
                            // Unselected link: standard sepia accent
                            // with a thin underline so links are
                            // visually distinguished from prose even
                            // before Tab discovers them.
                            c.setTextColor(kAccent, kBg);
                            c.setCursor(x, y);
                            c.print(seg.text);
                            c.drawLine(x, y + kLineH - 2,
                                       x + segW - 1, y + kLineH - 2,
                                       kAccentLo);
                        } else {
                            c.setTextColor(kIdle, kBg);
                            c.setCursor(x, y);
                            c.print(seg.text);
                        }
                        x += segW;
                    }
                } else {
                    c.setTextColor(kIdle, kBg);
                    c.setCursor(kPadX, y);
                    c.print(plainLines[idx]);
                }
            }

            // Two-row hint: contextual line surfaces the next-most-
            // useful action (more pages vs. where-next); persistent
            // line surfaces the global shortcuts so reroll is
            // discoverable without a manual.
            const char* topLeft;
            if (scrollLine + linesPerPage < totalLines) {
                topLeft = "enter . next page  ([ ] also)";
            } else {
                topLeft = "enter . pick what's next";
            }
            // When the article has tappable links, surface "tab links"
            // in the persistent hint row -- otherwise it's a lottery
            // for users to discover.
            bool hasLinks = useTokenView && !links.empty();
            const char* line2;
            if (trailDepth > 0 && hasLinks)
                line2 = "tab link . r reroll . s save . b trail";
            else if (trailDepth > 0)
                line2 = "r reroll . s save . b trail . del home";
            else if (hasLinks)
                line2 = "tab link . r reroll . s save . q qr";
            else
                line2 = "r reroll . s save . q qr . del back";
            drawHintBar(c, topLeft, saved ? "* saved" : nullptr, line2);

            // Vertical scrollbar at the right edge of the body
            // region. Always-on visual progress -- more readable at
            // a glance than the "1/5" page counter alone. Skip when
            // the whole article fits on one screen (nothing to
            // scroll past, no need for the visual).
            if (totalLines > linesPerPage) {
                constexpr int kBarX = kScreenW - 2;
                int barTop    = readY;
                int barHeight = readH;
                // Track (very dim)
                c.drawLine(kBarX, barTop, kBarX, barTop + barHeight - 1, kDivider);
                // Thumb: position = scrollLine/total, size = visible/total
                int thumbH = (linesPerPage * barHeight + totalLines / 2) / totalLines;
                if (thumbH < 6) thumbH = 6;
                int thumbY = (scrollLine * (barHeight - thumbH) +
                              (totalLines - linesPerPage) / 2)
                             / (totalLines - linesPerPage);
                if (thumbY < 0) thumbY = 0;
                if (thumbY > barHeight - thumbH) thumbY = barHeight - thumbH;
                c.drawLine(kBarX, barTop + thumbY,
                           kBarX, barTop + thumbY + thumbH - 1, kAccent);
            }

            c.pushSprite(0, 0);
            dirty = false;
        }

        auto k = readKeysEdge(prevAny, prevWord, prevEnter, prevDel, prevTab, prevEsc);
        for (char ch : k.word) {
            if (ch == ';' || ch == ',') {
                // up arrow / prev line
                if (scrollLine > 0) { --scrollLine; dirty = true; }
            } else if (ch == '.' || ch == '/') {
                // down arrow / next line
                if (scrollLine + linesPerPage < totalLines) { ++scrollLine; dirty = true; }
            } else if (ch == '[') {
                scrollLine = std::max(0, scrollLine - linesPerPage);
                dirty = true;
            } else if (ch == ']') {
                if (scrollLine + linesPerPage < totalLines) {
                    scrollLine = std::min(totalLines - linesPerPage,
                                          scrollLine + linesPerPage);
                    dirty = true;
                }
            } else if (ch == 's' || ch == 'S') {
                if (!saved) {
                    if (wiki_store::save(a)) { saved = true; sound::save(); }
                    dirty = true;
                }
                return ArticleAction::Save;
            } else if (ch == 'r' || ch == 'R') {
                sound::reroll();
                return ArticleAction::Random;
            } else if ((ch == 'b' || ch == 'B') && trailDepth > 0) {
                // Step backward through the walk -- only meaningful
                // if we have history.
                sound::back();
                return ArticleAction::TrailBack;
            } else if (ch == 'q' || ch == 'Q') {
                // QR share overlay -- modal; returns to this article
                // when dismissed. Re-prime key state so a held 'q'
                // doesn't immediately re-trigger.
                sound::open();
                showQrShare(canv, a);
                primeKeyEdge(prevWord, prevEnter, prevDel, prevTab, prevEsc);
                dirty = true;
            }
        }

        // Tab cycles through inline links: -1 -> 0 -> 1 -> ... -> N-1
        // -> -1 (back to "no selection"). Past-the-end wraps to
        // unselected rather than back to the first link so the user
        // has a way out of link-mode without hitting Esc.
        if (k.tab && useTokenView && !links.empty()) {
            if (selectedLink + 1 >= (int)links.size()) {
                selectedLink = -1;
            } else {
                ++selectedLink;
                int target = links[selectedLink].firstLine;
                if (target < scrollLine) {
                    scrollLine = target;
                } else if (target >= scrollLine + linesPerPage) {
                    scrollLine = std::max(0, target - linesPerPage + 1);
                    if (scrollLine + linesPerPage > totalLines) {
                        scrollLine = std::max(0, totalLines - linesPerPage);
                    }
                }
            }
            sound::nav();
            dirty = true;
        }

        if (k.enter) {
            // Enter on a selected link -> follow it. Returns Related
            // so the walk loop can fetch + push trail; the link's
            // pageId is shared via the outer-scope `followPageId` so
            // the walk loop can intercept and route directly to
            // fetchByPageId instead of going through the related
            // picker.
            if (useTokenView && selectedLink >= 0 &&
                selectedLink < (int)links.size()) {
                followPageIdOut = links[selectedLink].pageId;
                sound::open();
                return ArticleAction::Related;   // walk loop sees followPageIdOut
            }
            if (scrollLine + linesPerPage < totalLines) {
                scrollLine = std::min(totalLines - linesPerPage,
                                      scrollLine + linesPerPage);
                dirty = true;
            } else {
                sound::open();
                return ArticleAction::Related;
            }
        }
        if (k.esc || k.del) { sound::back(); return ArticleAction::Back; }
        delay(20);
    }
}

// ---------- settings ----------

// Settings list -- the single place where all togglable state is
// discoverable from the UI. Each row shows a label + current value;
// enter on a row cycles the value. Most settings are also accessible
// via direct hotkeys elsewhere (m for mute, p for people filter),
// but this screen surfaces text-size which has no shortcut and
// gives the user a place to look when they want to "change a thing."
void runSettings(Canvas& canv) {
    if (!canv.ok) return;
    auto& c = canv.c;

    int selected = 0;
    bool dirty = true;
    constexpr int kRowCount = 3;

    std::vector<char> prevWord;
    bool prevEnter = false, prevDel = false, prevTab = false, prevEsc = false, prevAny = false;
    primeKeyEdge(prevWord, prevEnter, prevDel, prevTab, prevEsc);

    auto cycleTextSize = []() {
        String cur = settings::textSize();
        const char* nxt = "small";
        if      (cur == "small")  nxt = "medium";
        else if (cur == "medium") nxt = "large";
        else                      nxt = "small";
        settings::setTextSize(String(nxt));
    };

    auto valueFor = [](int row) -> String {
        switch (row) {
            case 0: {
                String s = settings::textSize();
                if (s == "medium") return "medium";
                if (s == "large")  return "large";
                return "small";
            }
            case 1: return settings::peopleAllowed() ? "shown" : "hidden";
            case 2: return sound::muted() ? "muted" : "on";
        }
        return "";
    };

    struct Row { const char* label; };
    static const Row rows[kRowCount] = {
        {"text size"},
        {"people"},
        {"audio"},
    };

    while (true) {
        if (dirty) {
            c.fillScreen(kBg);
            drawStatusBar(c, "SETTINGS");

            // Title in serif italic to match the visual identity of
            // other "decision" screens (next-move picker, etc.)
            c.setFont(&fonts::FreeSerifBoldItalic9pt7b);
            c.setTextSize(1);
            c.setTextColor(kAccent, kBg);
            const char* t = "settings";
            int tw = c.textWidth(t);
            c.setCursor((kScreenW - tw) / 2, kBodyY + 4);
            c.print(t);

            // Rows: label left, value right, selected row in sepia bg.
            c.setFont(&fonts::Font2);
            c.setTextSize(1);
            int rowY = kBodyY + 28;
            constexpr int kRowH = 20;
            for (int i = 0; i < kRowCount; ++i) {
                bool sel = (i == selected);
                int y = rowY + i * kRowH;
                if (sel) {
                    c.fillRoundRect(kPadX + 4, y - 1,
                                    kScreenW - 2 * (kPadX + 4), 18, 3, kAccentLo);
                }
                c.setTextColor(sel ? kBg : kIdle, sel ? kAccentLo : kBg);
                c.setCursor(kPadX + 10, y + 1);
                c.print(rows[i].label);

                String val = valueFor(i);
                c.setTextColor(sel ? kBg : kAccent, sel ? kAccentLo : kBg);
                int vw = c.textWidth(val.c_str());
                c.setCursor(kScreenW - kPadX - 10 - vw, y + 1);
                c.print(val);
            }

            drawHintBar(c,
                        "arrows nav . enter change",
                        nullptr,
                        "del back to home");
            c.pushSprite(0, 0);
            dirty = false;
        }

        auto k = readKeysEdge(prevAny, prevWord, prevEnter, prevDel, prevTab, prevEsc);
        for (char ch : k.word) {
            if (ch == ';' || ch == ',') {
                if (selected > 0) { sound::nav(); --selected; dirty = true; }
            } else if (ch == '.' || ch == '/') {
                if (selected < kRowCount - 1) { sound::nav(); ++selected; dirty = true; }
            }
        }
        if (k.enter) {
            sound::nav();
            switch (selected) {
                case 0: cycleTextSize(); break;
                case 1: settings::setPeopleAllowed(!settings::peopleAllowed()); break;
                case 2: sound::setMuted(!sound::muted()); break;
            }
            dirty = true;
        }
        if (k.esc || k.del) { sound::back(); return; }
        delay(20);
    }
}

// ---------- QR share overlay ----------

// Shows the current article's wikipedia URL as a QR code. User scans
// with a phone to continue reading on a real browser / share to a
// friend. Blocks until any key is pressed, then returns. Doesn't
// touch the walk loop's state -- caller treats this as a pure
// modal sub-screen.
void showQrShare(Canvas& canv, const wiki::ArticleSummary& a) {
    if (!canv.ok) return;
    auto& c = canv.c;

    c.fillScreen(kBg);
    drawStatusBar(c, "SHARE");

    // Title above QR, ellipsized to one line
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(kAccent, kBg);
    String title = toAscii(String(a.title.c_str()));
    String shown = ellipsize(c, title, kScreenW - 2 * kPadX);
    int tw = c.textWidth(shown.c_str());
    c.setCursor((kScreenW - tw) / 2, kBodyY + 2);
    c.print(shown);

    // QR code: black-on-white for scannability. Wrap in a small
    // white quiet-zone so phone cameras can find the finder patterns
    // against the dark theme.
    constexpr int kQrW    = 78;
    constexpr int kQuiet  = 5;
    int qrX = (kScreenW - kQrW) / 2;
    int qrY = kBodyY + 14;
    c.fillRect(qrX - kQuiet, qrY - kQuiet,
               kQrW + 2 * kQuiet, kQrW + 2 * kQuiet, 0xFFFF);
    // Version 4 supports up to 78 alphanumeric chars at L correction --
    // enough for any wikipedia URL we'd render.
    c.qrcode(a.canonicalUrl.c_str(), qrX, qrY, kQrW, 4);

    // Footer label: the URL itself in dim, so the user can read it
    // even without scanning.
    c.setFont(&fonts::Font0);
    c.setTextColor(kDim, kBg);
    String url(a.canonicalUrl.c_str());
    String urlShort = ellipsize(c, url, kScreenW - 2 * kPadX);
    int uw = c.textWidth(urlShort.c_str());
    c.setCursor((kScreenW - uw) / 2, qrY + kQrW + kQuiet + 2);
    c.print(urlShort);

    drawHintBar(c, "scan from phone to open", nullptr, "any key to return");
    c.pushSprite(0, 0);

    // Block on any key
    std::vector<char> prevWord;
    bool prevEnter = false, prevDel = false, prevTab = false, prevEsc = false, prevAny = false;
    primeKeyEdge(prevWord, prevEnter, prevDel, prevTab, prevEsc);
    while (true) {
        auto k = readKeysEdge(prevAny, prevWord, prevEnter, prevDel, prevTab, prevEsc);
        if (k.enter || k.del || k.esc || !k.word.empty()) {
            sound::back();
            return;
        }
        delay(20);
    }
}

// ---------- next-move picker ----------

// Three forward intents the user can pick after finishing an article.
// Cancel means "back to the article I was just reading" -- distinct
// from Home, which would unwind the whole walk.
enum class NextChoice { Cancel, Wander, Random, Today };

// Shown after enter-at-end-of-article. Replaces the old auto-jump-to-
// related behavior so a fresh random is a single keypress away
// without escaping out to home first.
NextChoice runNextMove(Canvas& canv, const String& sourceTitle) {
    auto& c = canv.c;
    int selected = 0;  // default to wander -- contextual but not forced
    bool dirty = true;

    struct Option {
        char        key;
        const char* label;
        const char* hint;
        NextChoice  choice;
    };
    Option opts[] = {
        {'1', "wander from this", "find related articles",  NextChoice::Wander},
        {'2', "random article",   "stumble somewhere new",  NextChoice::Random},
        {'3', "today's events",   "what happened today",    NextChoice::Today},
    };
    constexpr int kOptCount = sizeof(opts) / sizeof(opts[0]);

    std::vector<char> prevWord;
    bool prevEnter = false, prevDel = false, prevTab = false, prevEsc = false, prevAny = false;
    primeKeyEdge(prevWord, prevEnter, prevDel, prevTab, prevEsc);

    while (true) {
        if (dirty) {
            c.fillScreen(kBg);
            drawStatusBar(c, "NEXT MOVE");

            // Title block: serif italic restatement, dim "from:" line
            c.setFont(&fonts::FreeSerifBoldItalic9pt7b);
            c.setTextSize(1);
            c.setTextColor(kAccent, kBg);
            const char* t = "what next?";
            int tw = c.textWidth(t);
            c.setCursor((kScreenW - tw) / 2, kBodyY + 2);
            c.print(t);

            c.setFont(&fonts::Font0);
            c.setTextColor(kDim, kBg);
            String from = String("from: ") + sourceTitle;
            c.setCursor(kPadX, kBodyY + 18);
            c.print(ellipsize(c, from, kScreenW - 2 * kPadX));

            // Option rows
            c.setFont(&fonts::Font2);
            int rowY = kBodyY + 32;
            constexpr int kRowH = 22;
            for (int i = 0; i < kOptCount; ++i) {
                bool sel = (i == selected);
                int y = rowY + i * kRowH;
                if (sel) {
                    c.fillRoundRect(kPadX + 4, y - 2,
                                    kScreenW - 2 * (kPadX + 4), 20, 3, kAccentLo);
                }
                char keyStr[6];
                snprintf(keyStr, sizeof(keyStr), "[%c]", opts[i].key);
                c.setTextColor(sel ? kBg : kAccent, sel ? kAccentLo : kBg);
                c.setCursor(kPadX + 10, y);
                c.print(keyStr);
                c.setTextColor(sel ? kBg : kIdle, sel ? kAccentLo : kBg);
                c.setCursor(kPadX + 38, y);
                c.print(opts[i].label);
            }

            drawHintBar(c,
                        "1-3 jump . arrows nav",
                        nullptr,
                        "enter pick . del back to article");
            c.pushSprite(0, 0);
            dirty = false;
        }

        auto k = readKeysEdge(prevAny, prevWord, prevEnter, prevDel, prevTab, prevEsc);
        for (char ch : k.word) {
            if (ch == ';' || ch == ',') {
                if (selected > 0) { sound::nav(); --selected; dirty = true; }
            } else if (ch == '.' || ch == '/') {
                if (selected < kOptCount - 1) { sound::nav(); ++selected; dirty = true; }
            } else if (ch >= '1' && ch <= '0' + kOptCount) {
                int idx = ch - '1';
                sound::open();
                return opts[idx].choice;
            }
        }
        if (k.enter) {
            sound::open();
            return opts[selected].choice;
        }
        if (k.esc || k.del) {
            sound::back();
            return NextChoice::Cancel;
        }
        delay(20);
    }
}

// ---------- related picker ----------

// Returns the selected pageId (UTF-8) or empty string if user backed out.
String runRelated(Canvas& canv, const String& sourceTitle,
                  const std::vector<wiki::RelatedItem>& items) {
    if (!canv.ok || items.empty()) return String();
    auto& c = canv.c;

    int selected = 0;
    int scroll   = 0;
    constexpr int kRowH = 16;
    int rowsVisible = (kBodyH - 16) / kRowH;
    if (rowsVisible < 1) rowsVisible = 1;
    bool dirty = true;

    std::vector<char> prevWord;
    bool prevEnter = false, prevDel = false, prevTab = false, prevEsc = false, prevAny = false;
    primeKeyEdge(prevWord, prevEnter, prevDel, prevTab, prevEsc);

    while (true) {
        if (dirty) {
            c.fillScreen(kBg);
            drawStatusBar(c, "WHERE NEXT?");

            // Source label
            c.setFont(&fonts::Font0);
            c.setTextColor(kDim, kBg);
            String src = String("from: ") + sourceTitle;
            String srcShort = ellipsize(c, src, kScreenW - 2 * kPadX);
            c.setCursor(kPadX, kBodyY + 2);
            c.print(srcShort);

            // Items
            int startY = kBodyY + 16;
            c.setFont(&fonts::Font2);
            for (int i = 0; i < rowsVisible && (scroll + i) < (int)items.size(); ++i) {
                int idx = scroll + i;
                bool sel = (idx == selected);
                int y = startY + i * kRowH;
                if (sel) c.fillRoundRect(kPadX, y - 1,
                                         kScreenW - 2 * kPadX, kRowH - 1, 2, kAccentLo);
                char buf[8]; snprintf(buf, sizeof(buf), "%d.", idx + 1);
                c.setTextColor(sel ? kBg : kAccent, sel ? kAccentLo : kBg);
                c.setCursor(kPadX + 4, y + 1);
                c.print(buf);
                c.setTextColor(sel ? kBg : kIdle, sel ? kAccentLo : kBg);
                String title = toAscii(String(items[idx].title.c_str()));
                String show = ellipsize(c, title, kScreenW - 2 * kPadX - 26);
                c.setCursor(kPadX + 22, y + 1);
                c.print(show);
            }
            char hintR[16];
            snprintf(hintR, sizeof(hintR), "%d/%zu", selected + 1, items.size());
            drawHintBar(c,
                        "1-6 jump . enter open",
                        hintR,
                        "arrows nav . del back");
            c.pushSprite(0, 0);
            dirty = false;
        }

        auto k = readKeysEdge(prevAny, prevWord, prevEnter, prevDel, prevTab, prevEsc);
        for (char ch : k.word) {
            if (ch == ';' || ch == ',') {
                if (selected > 0) { --selected; dirty = true; }
                if (selected < scroll) scroll = selected;
            } else if (ch == '.' || ch == '/') {
                if (selected < (int)items.size() - 1) { ++selected; dirty = true; }
                if (selected >= scroll + rowsVisible) scroll = selected - rowsVisible + 1;
            } else if (ch >= '1' && ch <= '6') {
                int idx = ch - '1';
                if (idx < (int)items.size()) {
                    return String(items[idx].pageId.c_str());
                }
            }
        }
        if (k.enter)        return String(items[selected].pageId.c_str());
        if (k.esc || k.del) return String();
        delay(20);
    }
}

// ---------- search ----------

// Returns selected pageId or empty.
//
// Dual-mode UX: focus alternates between the input field (text edits
// the query) and the results list (arrows nav, enter opens
// highlighted, typing any letter snaps focus back to input + appends).
// Tab toggles. Caret blinks only in input focus.
String runSearch(Canvas& canv, wiki::WikiClient& client) {
    if (!canv.ok) return String();
    auto& c = canv.c;

    String query;
    std::vector<wiki::SearchHit> hits;
    int selected = 0;
    int scroll   = 0;
    constexpr int kRowH = 14;
    bool dirty = true;
    bool inputFocused = true;
    String lastSearched;
    uint32_t lastEdit = 0;
    bool searching = false;

    std::vector<char> prevWord;
    bool prevEnter = false, prevDel = false, prevTab = false, prevEsc = false, prevAny = false;
    primeKeyEdge(prevWord, prevEnter, prevDel, prevTab, prevEsc);

    while (true) {
        // Debounced live-search: if user hasn't typed in 600ms and the
        // query changed since the last search, fire it now.
        if (query.length() >= 2 && query != lastSearched &&
            millis() - lastEdit > 600 && !searching) {
            searching = true;
            pushLoading(canv, "searching...");
            std::vector<wiki::SearchHit> next;
            bool ok = client.search(query.c_str(), next, 10);
            if (ok) {
                hits = std::move(next);
                selected = 0;
                scroll = 0;
            }
            lastSearched = query;
            searching = false;
            dirty = true;
        }

        if (dirty) {
            c.fillScreen(kBg);
            // Status right side: hit count when results are in, focus
            // hint otherwise. Reduces "did my search return anything?"
            // ambiguity at a glance.
            char rightBuf[20];
            if (!hits.empty()) {
                snprintf(rightBuf, sizeof(rightBuf), "%zu hits",
                         hits.size());
            } else {
                snprintf(rightBuf, sizeof(rightBuf), "%s",
                         inputFocused ? "typing..." : "picking");
            }
            drawStatusBar(c, "SEARCH", rightBuf);

            // Input field. Outline color signals whether focus is
            // here: bright sepia when input has focus, dim when list.
            c.setFont(&fonts::Font2);
            c.setTextColor(kIdle, kBg);
            c.fillRoundRect(kPadX, kBodyY + 2, kScreenW - 2 * kPadX, 16, 3, kPanel);
            c.drawRoundRect(kPadX, kBodyY + 2, kScreenW - 2 * kPadX, 16, 3,
                            inputFocused ? kAccent : kAccentLo);
            c.setCursor(kPadX + 6, kBodyY + 4);
            String show = query;
            if (show.length() == 0) {
                c.setTextColor(kDim, kPanel);
                c.print("type to search...");
            } else {
                c.setTextColor(kIdle, kPanel);
                c.print(show);
                // Blinking caret only when input is focused.
                if (inputFocused && (millis() / 400) % 2 == 0) {
                    int cw = c.textWidth(show.c_str());
                    c.fillRect(kPadX + 6 + cw, kBodyY + 4, 1, 12, kAccent);
                }
            }

            // Results
            int startY = kBodyY + 22;
            int rowsVisible = (kBodyH - 22) / kRowH;
            for (int i = 0; i < rowsVisible && (scroll + i) < (int)hits.size(); ++i) {
                int idx = scroll + i;
                bool sel = (idx == selected) && !inputFocused;
                int y = startY + i * kRowH;
                if (sel) c.fillRoundRect(kPadX, y - 1,
                                         kScreenW - 2 * kPadX, kRowH - 1, 2, kAccentLo);
                c.setFont(&fonts::Font2);
                c.setTextSize(1);
                c.setTextColor(sel ? kBg : kIdle, sel ? kAccentLo : kBg);
                c.setCursor(kPadX + 4, y);
                String title = toAscii(String(hits[idx].title.c_str()));
                c.print(ellipsize(c, title, kScreenW - 2 * kPadX - 8));
            }
            if (hits.empty() && query.length() >= 2 && query == lastSearched) {
                c.setFont(&fonts::Font0);
                c.setTextColor(kDim, kBg);
                c.setCursor(kPadX, startY + 4);
                c.print("(no results)");
            }

            const char* l1;
            const char* l2;
            if (inputFocused) {
                l1 = hits.empty() ? "type to search"
                                  : "enter . top hit  .  tab . pick";
                l2 = "del erase . ` back";
            } else {
                l1 = "arrows . pick  enter open";
                l2 = "tab . back to typing . ` back";
            }
            drawHintBar(c, l1, nullptr, l2);
            c.pushSprite(0, 0);
            dirty = false;
        }

        auto k = readKeysEdge(prevAny, prevWord, prevEnter, prevDel, prevTab, prevEsc);

        // Tab always toggles focus.
        if (k.tab) {
            // Only honor the toggle when there's something to switch
            // to: with empty results, list-focus would be a dead end.
            if (!hits.empty()) {
                inputFocused = !inputFocused;
                dirty = true;
            }
        }

        if (inputFocused) {
            // Typing edits the query; printable ASCII only.
            for (char ch : k.word) {
                if (ch >= ' ' && ch < 0x7F && ch != '`') {
                    query += ch;
                    lastEdit = millis();
                    dirty = true;
                }
            }
            if (k.del && query.length() > 0) {
                query.remove(query.length() - 1);
                lastEdit = millis();
                dirty = true;
            }
            if (k.enter && !hits.empty()) {
                return String(hits[0].pageId.c_str());   // top result
            }
        } else {
            // List focus. Arrows nav, 1-9 jump, typing any letter
            // returns to input mode AND appends so users can refine
            // mid-pick without an explicit Tab.
            for (char ch : k.word) {
                if (ch == ';' || ch == ',') {
                    if (selected > 0) { --selected; dirty = true; }
                    if (selected < scroll) scroll = selected;
                } else if (ch == '.' || ch == '/') {
                    if (selected < (int)hits.size() - 1) { ++selected; dirty = true; }
                    int rowsVisible = (kBodyH - 22) / kRowH;
                    if (selected >= scroll + rowsVisible) scroll = selected - rowsVisible + 1;
                } else if (ch >= '1' && ch <= '9') {
                    int idx = ch - '1';
                    if (idx < (int)hits.size()) {
                        return String(hits[idx].pageId.c_str());
                    }
                } else if (ch >= ' ' && ch < 0x7F && ch != '`') {
                    // Any other letter: snap back to input, append.
                    inputFocused = true;
                    query += ch;
                    lastEdit = millis();
                    dirty = true;
                }
            }
            if (k.del) {
                // Erase one char and snap back to input.
                inputFocused = true;
                if (query.length() > 0) {
                    query.remove(query.length() - 1);
                    lastEdit = millis();
                }
                dirty = true;
            }
            if (k.enter && !hits.empty()) {
                return String(hits[selected].pageId.c_str());
            }
        }

        if (k.esc) return String();

        // Caret blink tick (only matters in input focus -- innocuous
        // re-render otherwise).
        static uint32_t lastTick = 0;
        if (millis() - lastTick > 400) { dirty = true; lastTick = millis(); }
        delay(20);
    }
}

// ---------- On This Day picker ----------

// Forward decl: showError is defined later in this namespace.
void showError(Canvas& canv, const char* head, const String& detail);

// Returns selected pageId of the event the user picked, or empty if
// they backed out / no events available.
String runToday(Canvas& canv, wiki::WikiClient& client) {
    if (!canv.ok) return String();
    auto& c = canv.c;

    // Use device's local time for the date (NTP-synced at boot).
    time_t now = time(nullptr);
    struct tm tmnow;
    localtime_r(&now, &tmnow);
    int month = tmnow.tm_mon + 1;
    int day   = tmnow.tm_mday;

    // Fetch events. Streamed parse keeps RAM bounded even though the
    // raw response is ~200KB on the wire.
    pushLoading(canv, "loading today...");
    std::vector<wiki::TodayEvent> events;
    if (!client.fetchOnThisDay(month, day, events, 20)) {
        showError(canv, "today failed", String(client.lastError().c_str()));
        return String();
    }
    if (events.empty()) {
        showError(canv, "nothing for today", "wikipedia returned no events");
        return String();
    }

    // Date label like "MAY 27"
    static const char* kMonths[] = {
        "JAN","FEB","MAR","APR","MAY","JUN",
        "JUL","AUG","SEP","OCT","NOV","DEC"
    };
    char dateLabel[16];
    snprintf(dateLabel, sizeof(dateLabel), "%s %d", kMonths[month - 1], day);

    int selected = 0;
    int scroll   = 0;
    constexpr int kRowH = 24;     // bigger row -- two lines of text per event
    int rowsVisible = (kBodyH - 4) / kRowH;
    if (rowsVisible < 1) rowsVisible = 1;
    bool dirty = true;

    std::vector<char> prevWord;
    bool prevEnter = false, prevDel = false, prevTab = false, prevEsc = false, prevAny = false;
    primeKeyEdge(prevWord, prevEnter, prevDel, prevTab, prevEsc);

    while (true) {
        if (dirty) {
            c.fillScreen(kBg);
            char rb[24];
            snprintf(rb, sizeof(rb), "%zu events", events.size());
            drawStatusBar(c, dateLabel, rb);

            for (int i = 0; i < rowsVisible && (scroll + i) < (int)events.size(); ++i) {
                int idx = scroll + i;
                bool sel = (idx == selected);
                int y = kBodyY + 2 + i * kRowH;
                if (sel) c.fillRoundRect(kPadX, y - 1,
                                         kScreenW - 2 * kPadX, kRowH - 1, 3, kAccentLo);

                // Year prefix (right side of row)
                c.setFont(&fonts::Font2);
                c.setTextSize(1);
                char yearBuf[12];
                if (events[idx].year > 0) {
                    snprintf(yearBuf, sizeof(yearBuf), "%d", events[idx].year);
                } else if (events[idx].year < 0) {
                    snprintf(yearBuf, sizeof(yearBuf), "%d BC", -events[idx].year);
                } else {
                    yearBuf[0] = 0;
                }
                c.setTextColor(sel ? kBg : kAccent, sel ? kAccentLo : kBg);
                c.setCursor(kPadX + 4, y);
                c.print(yearBuf);

                // Title (first line, prominent)
                c.setTextColor(sel ? kBg : kIdle, sel ? kAccentLo : kBg);
                c.setCursor(kPadX + 56, y);
                String title = toAscii(String(events[idx].title.c_str()));
                c.print(ellipsize(c, title, kScreenW - kPadX - 60));

                // Event text (second line, dim, truncated)
                c.setFont(&fonts::Font0);
                c.setTextColor(sel ? kBg : kDim, sel ? kAccentLo : kBg);
                c.setCursor(kPadX + 4, y + 13);
                String txt = toAscii(String(events[idx].text.c_str()));
                c.print(ellipsize(c, txt, kScreenW - 2 * kPadX - 4));
            }

            char hintR[16];
            snprintf(hintR, sizeof(hintR), "%d/%zu", selected + 1, events.size());
            drawHintBar(c,
                        "arrows nav . enter open",
                        hintR,
                        "del back to home");
            c.pushSprite(0, 0);
            dirty = false;
        }

        auto k = readKeysEdge(prevAny, prevWord, prevEnter, prevDel, prevTab, prevEsc);
        for (char ch : k.word) {
            if (ch == ';' || ch == ',') {
                if (selected > 0) { sound::nav(); --selected; dirty = true; }
                if (selected < scroll) scroll = selected;
            } else if (ch == '.' || ch == '/') {
                if (selected < (int)events.size() - 1) { sound::nav(); ++selected; dirty = true; }
                if (selected >= scroll + rowsVisible) scroll = selected - rowsVisible + 1;
            }
        }
        if (k.enter && !events.empty()) {
            sound::open();
            return String(events[selected].pageId.c_str());
        }
        if (k.esc || k.del) { sound::back(); return String(); }
        delay(20);
    }
}

// ---------- saved browser ----------

// Returns selected slug or empty.
String runSaved(Canvas& canv) {
    if (!canv.ok) return String();
    auto& c = canv.c;
    auto items = wiki_store::list();
    int selected = 0;
    int scroll   = 0;
    constexpr int kRowH = 16;
    int rowsVisible = (kBodyH - 4) / kRowH;
    bool dirty = true;

    std::vector<char> prevWord;
    bool prevEnter = false, prevDel = false, prevTab = false, prevEsc = false, prevAny = false;
    primeKeyEdge(prevWord, prevEnter, prevDel, prevTab, prevEsc);

    while (true) {
        if (dirty) {
            c.fillScreen(kBg);
            char rb[16];
            snprintf(rb, sizeof(rb), "%zu saved", items.size());
            drawStatusBar(c, "LIBRARY", rb);

            if (items.empty()) {
                c.setFont(&fonts::Font2);
                c.setTextColor(kDim, kBg);
                const char* msg = "(no saved articles yet)";
                int tw = c.textWidth(msg);
                c.setCursor((kScreenW - tw) / 2, kBodyY + kBodyH / 2 - 6);
                c.print(msg);
            } else {
                // Reserve right edge for the YYYY-MM-DD date stamp
                // so the saved list reads as "library of dated
                // entries" rather than a flat list of titles.
                constexpr int kDateW = 56;
                for (int i = 0; i < rowsVisible && (scroll + i) < (int)items.size(); ++i) {
                    int idx = scroll + i;
                    bool sel = (idx == selected);
                    int y = kBodyY + 2 + i * kRowH;
                    if (sel) c.fillRoundRect(kPadX, y - 1,
                                             kScreenW - 2 * kPadX, kRowH - 1, 2, kAccentLo);
                    c.setFont(&fonts::Font2);
                    c.setTextSize(1);
                    c.setTextColor(sel ? kBg : kIdle, sel ? kAccentLo : kBg);
                    c.setCursor(kPadX + 4, y);
                    c.print(ellipsize(c, items[idx].title,
                                      kScreenW - 2 * kPadX - 8 - kDateW));
                    // Date: first 10 chars of saved_utc (YYYY-MM-DD).
                    String date = items[idx].savedUtc.substring(0, 10);
                    if (date.length() == 10) {
                        c.setFont(&fonts::Font0);
                        c.setTextColor(sel ? kBg : kDim, sel ? kAccentLo : kBg);
                        int dw = c.textWidth(date.c_str());
                        c.setCursor(kScreenW - dw - kPadX - 4, y + 4);
                        c.print(date);
                    }
                }
            }

            drawHintBar(c,
                        items.empty() ? "no saved articles" : "arrows nav . enter open",
                        nullptr,
                        items.empty() ? "del back to home" : "d delete . del back");
            c.pushSprite(0, 0);
            dirty = false;
        }

        auto k = readKeysEdge(prevAny, prevWord, prevEnter, prevDel, prevTab, prevEsc);
        for (char ch : k.word) {
            if (ch == ';' || ch == ',') {
                if (selected > 0) { --selected; dirty = true; }
                if (selected < scroll) scroll = selected;
            } else if (ch == '.' || ch == '/') {
                if (selected < (int)items.size() - 1) { ++selected; dirty = true; }
                if (selected >= scroll + rowsVisible) scroll = selected - rowsVisible + 1;
            } else if (ch == 'd' || ch == 'D') {
                if (!items.empty()) {
                    wiki_store::remove(items[selected].slug);
                    items = wiki_store::list();
                    if (selected >= (int)items.size()) selected = std::max(0, (int)items.size() - 1);
                    dirty = true;
                }
            }
        }
        if (k.enter && !items.empty()) {
            return items[selected].slug;
        }
        // del key (a distinct hardware key from the 'd' char) acts as
        // back here. 'd' character (lowercase) is the delete-current
        // shortcut, handled via the word loop above.
        if (k.esc || k.del) return String();
        delay(20);
    }
}

// ---------- error / info banner ----------

void showError(Canvas& canv, const char* head, const String& detail) {
    if (!canv.ok) return;
    auto& c = canv.c;
    c.fillScreen(kBg);
    drawStatusBar(c, "ERROR");
    c.setFont(&fonts::Font2);
    c.setTextColor(kErr, kBg);
    int hw = c.textWidth(head);
    c.setCursor((kScreenW - hw) / 2, kBodyY + 14);
    c.print(head);
    c.setFont(&fonts::Font0);
    c.setTextColor(kDim, kBg);
    auto lines = wrap(c, detail, kScreenW - 2 * kPadX);
    for (size_t i = 0; i < lines.size() && i < 4; ++i) {
        c.setCursor(kPadX, kBodyY + 36 + (int)i * 10);
        c.print(lines[i]);
    }
    drawHintBar(c, "any key to return");
    c.pushSprite(0, 0);
    sound::error();

    std::vector<char> prevWord;
    bool prevEnter = false, prevDel = false, prevTab = false, prevEsc = false, prevAny = false;
    primeKeyEdge(prevWord, prevEnter, prevDel, prevTab, prevEsc);
    while (true) {
        auto k = readKeysEdge(prevAny, prevWord, prevEnter, prevDel, prevTab, prevEsc);
        if (k.enter || k.del || k.esc || !k.word.empty()) return;
        delay(20);
    }
}

} // namespace

// ---------- top-level orchestrator ----------

void run(wiki::WikiClient& client) {
    Canvas canv;
    if (!canv.ok) {
        Serial.println("[wiki] canvas alloc failed; bailing");
        return;
    }

    // Single article + unified walk loop. Random / Search / Saved all
    // converge into the same reader+walker -- a saved article that
    // the user wants to "reroll" out of just transitions cleanly into
    // the random fetch, no awkward bounce-to-home.

    auto showLastError = [&](const char* fallbackHead) {
        String detail(client.lastError().c_str());
        showError(canv,
                  detail.length() ? prettyHead(detail) : fallbackHead,
                  detail.length() ? detail : String(fallbackHead));
    };

    // Wrap fetchRandom with the people-skip filter. Re-rolls up to
    // kFilterTries times when biography is detected and the user
    // has people disabled; otherwise transparent passthrough.
    constexpr int kFilterTries = 5;
    auto fetchRandomFiltered = [&](wiki::ArticleSummary& out) -> bool {
        if (settings::peopleAllowed()) {
            return client.fetchRandom(out);
        }
        for (int i = 0; i < kFilterTries; ++i) {
            if (!client.fetchRandom(out)) return false;
            if (!looksLikePerson(out.description)) return true;
            Serial.printf("[filter] skip person: %s\n", out.title.c_str());
        }
        // Gave up after N -- return whatever we got rather than fail.
        return true;
    };

    // Cap trail depth: each ArticleSummary holds ~3-5 KB of strings,
    // so an uncapped trail eats heap fast on long sessions. After
    // the cap, oldest articles drop off -- still plenty of history
    // for "where did I just come from?" without fragmenting heap.
    constexpr size_t kMaxTrail = 12;
    auto pushTrail = [](std::vector<wiki::ArticleSummary>& trail,
                        const wiki::ArticleSummary& a) {
        trail.push_back(a);
        if (trail.size() > kMaxTrail) trail.erase(trail.begin());
    };

    while (true) {
        HomeChoice choice = runHome(canv);
        if (choice == HomeChoice::Exit) return;
        if (choice == HomeChoice::Settings) {
            runSettings(canv);
            continue;
        }

        wiki::ArticleSummary a;
        bool haveArticle = false;

        if (choice == HomeChoice::Resume) {
            String pid = settings::lastPageId();
            if (pid.length() == 0) continue;   // shouldn't happen, defensive
            pushLoading(canv, "resuming...");
            if (!client.fetchByPageId(pid.c_str(), a)) {
                showLastError("fetch failed");
                // If the article is gone, clear the resume affordance
                // so it doesn't keep luring the user into a dead end.
                if (String(client.lastError().c_str()).startsWith("http 4")) {
                    settings::setLastPageId("");
                    settings::setLastTitle("");
                }
                continue;
            }
            haveArticle = true;
        } else if (choice == HomeChoice::Random) {
            pushLoading(canv, "fetching random...");
            if (!fetchRandomFiltered(a)) { showLastError("fetch failed"); continue; }
            haveArticle = true;
        } else if (choice == HomeChoice::Today) {
            String pid = runToday(canv, client);
            if (pid.length() == 0) continue;
            pushLoading(canv, "opening...");
            if (!client.fetchByPageId(pid.c_str(), a)) { showLastError("fetch failed"); continue; }
            haveArticle = true;
        } else if (choice == HomeChoice::Search) {
            String pid = runSearch(canv, client);
            if (pid.length() == 0) continue;
            pushLoading(canv, "opening...");
            if (!client.fetchByPageId(pid.c_str(), a)) { showLastError("fetch failed"); continue; }
            haveArticle = true;
        } else if (choice == HomeChoice::Saved) {
            String slug = runSaved(canv);
            if (slug.length() == 0) continue;
            if (!wiki_store::load(slug, a)) {
                showError(canv, "load failed", "could not read saved article");
                continue;
            }
            haveArticle = true;
        }
        if (!haveArticle) continue;

        // Walk loop. The trail is an in-memory breadcrumb of articles
        // visited before the current one in THIS walk -- forward
        // actions (random / where-next) push the current article so
        // the user can step back through their journey with `b`.
        // Resets to empty whenever we return to home.
        std::vector<wiki::ArticleSummary> trail;
        bool stayInWalk = true;
        while (stayInWalk) {
            settings::setLastPageId(String(a.pageId.c_str()));
            settings::setLastTitle(String(a.title.c_str()));
            bool savedAlready = wiki_store::exists(String(a.pageId.c_str()));
            String followPageId;
            ArticleAction act = runArticle(canv, a, savedAlready,
                                           (int)trail.size(), followPageId);

            // Inline-link follow short-circuits the related picker:
            // the user already chose a specific pageId via Tab + enter,
            // so we fetch that directly.
            if (act == ArticleAction::Related && followPageId.length() > 0) {
                pushLoading(canv, "opening...");
                wiki::ArticleSummary nextA;
                if (!client.fetchByPageId(followPageId.c_str(), nextA)) {
                    showLastError("fetch failed");
                    continue;
                }
                pushTrail(trail, a);
                a = nextA;
                continue;
            }

            if (act == ArticleAction::Back) {
                stayInWalk = false;
            } else if (act == ArticleAction::TrailBack) {
                if (!trail.empty()) {
                    a = trail.back();
                    trail.pop_back();
                }
            } else if (act == ArticleAction::Random) {
                pushLoading(canv, "fetching random...");
                wiki::ArticleSummary next;
                if (!fetchRandomFiltered(next)) {
                    showLastError("fetch failed");
                    continue;
                }
                pushTrail(trail, a);
                a = next;
            } else if (act == ArticleAction::Save) {
                // already saved inside runArticle; just re-render
            } else {  // ArticleAction::Related -- open the next-move picker
                NextChoice nc = runNextMove(canv, toAscii(String(a.title.c_str())));
                if (nc == NextChoice::Cancel) continue;   // back to article

                if (nc == NextChoice::Wander) {
                    pushLoading(canv, "finding related...");
                    std::vector<wiki::RelatedItem> rel;
                    if (!client.fetchMoreLike(a.pageId, rel)) {
                        showLastError("no related");
                        continue;
                    }
                    if (rel.empty()) {
                        showError(canv, "dead end",
                                  "this article has no related links");
                        continue;
                    }
                    String next = runRelated(canv, toAscii(String(a.title.c_str())), rel);
                    if (next.length() == 0) continue;
                    pushLoading(canv, "opening...");
                    wiki::ArticleSummary nextA;
                    if (!client.fetchByPageId(next.c_str(), nextA)) {
                        showLastError("fetch failed");
                        continue;
                    }
                    pushTrail(trail, a);
                    a = nextA;
                } else if (nc == NextChoice::Random) {
                    pushLoading(canv, "fetching random...");
                    wiki::ArticleSummary next;
                    if (!fetchRandomFiltered(next)) {
                        showLastError("fetch failed");
                        continue;
                    }
                    pushTrail(trail, a);
                    a = next;
                } else if (nc == NextChoice::Today) {
                    String pid = runToday(canv, client);
                    if (pid.length() == 0) continue;
                    pushLoading(canv, "opening...");
                    wiki::ArticleSummary nextA;
                    if (!client.fetchByPageId(pid.c_str(), nextA)) {
                        showLastError("fetch failed");
                        continue;
                    }
                    pushTrail(trail, a);
                    a = nextA;
                }
            }
        }
    }
}

} // namespace wiki_ui
