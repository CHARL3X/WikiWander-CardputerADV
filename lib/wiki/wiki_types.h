// Pure data shapes for Wikipedia API responses. No Arduino / M5 / FS
// dependencies -- this header is the contract the device UI and the
// native test harness both consume.
//
// All strings are std::string so the same code compiles for native PC
// tests and the ESP32-S3 device build (arduino-esp32 ships libstdc++).
//
// Field selection is opinionated: we keep only what the UI actually
// renders. Wikipedia's summary response has ~18 top-level keys; we
// take 5. Everything not surfaced is dropped at parse time so the
// in-memory footprint stays small.
#pragma once

#include <string>
#include <vector>

namespace wiki {

// Result of /api/rest_v1/page/summary/<title> or /random/summary.
struct ArticleSummary {
    std::string pageId;        // titles.canonical -- URL slug ("Hypertext")
    std::string title;         // display title    -- "Hypertext"
    std::string description;   // short subtitle   -- may be empty
    std::string extract;       // body text, plaintext UTF-8, 1-3 paragraphs
    std::string extractHtml;   // body in HTML; carries <a href="/wiki/...">
                               // tags so the reader can highlight + follow
                               // inline links. Empty for saved articles
                               // that pre-date the html field.
    std::string canonicalUrl;  // content_urls.desktop.page -- full https URL
};

// One entry in a "where next" picker, built from MediaWiki's morelike
// search (`action=query&list=search&srsearch=morelike:<title>`). We
// keep this shape compatible with future swap-in of category-walk or
// HTML-extracted inline links -- only the populator changes.
struct RelatedItem {
    std::string pageId;        // search result title, URL-safe ("History of hypertext")
    std::string title;         // display title
    std::string snippet;       // MediaWiki search snippet, plain text (HTML stripped)
};

// One entry from opensearch (`action=opensearch&search=<q>`). Returned
// in the API's 4-array shape; we flatten to a struct list.
struct SearchHit {
    std::string pageId;        // derived from URL slug
    std::string title;
    std::string description;   // usually empty for opensearch but kept for shape parity
};

// One event from /feed/onthisday/selected/<MM>/<DD>. Wikipedia's feed
// bundles each event with full ArticleSummary objects in a `pages`
// array; we keep only the primary page's identity so the response
// can be filtered down from ~200KB to a few KB at parse time.
struct TodayEvent {
    int         year;          // 4-digit, may be negative (BC events)
    std::string text;          // human description ("Apollo 10 launched...")
    std::string pageId;        // primary article pageId (titles.canonical)
    std::string title;         // primary article display title
};

} // namespace wiki
