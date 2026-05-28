// Wikipedia JSON -> wiki types. Single source of truth for what the
// rest of the app sees -- HTML stripping, slug derivation, and field
// selection all happen here.
//
// Failure mode: every parser returns a bool and never throws. On
// false return, the output struct is left in an unspecified state
// (don't read its fields). The caller decides whether to surface
// "couldn't parse" to the user or retry.
#pragma once

#include "wiki_types.h"
#include "transport.h"
#include <string>

namespace wiki {

// Parse /api/rest_v1/page/summary/<title> or /random/summary response.
bool parseSummary(const std::string& json, ArticleSummary& out);

// Parse /feed/onthisday/selected/<MM>/<DD> -- a curated event list
// where each entry has year, text, and one or more linked articles.
// Reads from a ByteReader so the network bytes can be streamed and
// filtered (the unfiltered response is ~200KB; ArduinoJson's filter
// drops the heavy fields on the fly so the in-memory document stays
// well under 10KB).
bool parseOnThisDay(ByteReader& src,
                    std::vector<TodayEvent>& out,
                    size_t maxItems = 30);

// Parse `action=query&list=search&srsearch=morelike:<title>` response.
// Pushes up to `maxItems` results into `out`. Returns true if the
// JSON shape was recognized (even if zero results).
bool parseMoreLike(const std::string& json,
                   std::vector<RelatedItem>& out,
                   size_t maxItems = 6);

// Parse `action=opensearch&search=<q>` response. Same contract as
// parseMoreLike.
bool parseOpenSearch(const std::string& json,
                     std::vector<SearchHit>& out,
                     size_t maxItems = 10);

// Helpers exposed for testing. Don't depend on these from UI code.
namespace detail {
// Strip MediaWiki search snippet HTML (mainly <span class="searchmatch">
// wrappers) and decode common entities. Returns plain UTF-8.
std::string stripHtml(const std::string& s);
// Extract the trailing path segment of a Wikipedia article URL.
// "https://en.wikipedia.org/wiki/Carl_Peters" -> "Carl_Peters"
std::string slugFromUrl(const std::string& url);
} // namespace detail

} // namespace wiki
