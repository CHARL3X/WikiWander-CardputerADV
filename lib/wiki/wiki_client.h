// High-level Wikipedia client: builds URLs, calls the Transport,
// hands the JSON to the parsers, surfaces typed results.
//
// Single source of truth for endpoint shapes -- swap one URL here
// when MediaWiki deprecates something else, no UI changes needed.
#pragma once

#include "transport.h"
#include "wiki_types.h"
#include <string>
#include <vector>

namespace wiki {

class WikiClient {
public:
    // userAgent must be a descriptive string per Wikipedia's policy
    // (an anonymous UA returns 403). Convention is
    //   "ProductName/Version (contact-or-repo-url)"
    WikiClient(Transport* transport, std::string userAgent);

    bool fetchRandom(ArticleSummary& out);
    bool fetchByPageId(const std::string& pageId, ArticleSummary& out);
    bool fetchMoreLike(const std::string& pageId,
                       std::vector<RelatedItem>& out,
                       size_t maxItems = 6);
    bool search(const std::string& query,
                std::vector<SearchHit>& out,
                size_t maxItems = 10);

    // Fetches the curated "On This Day" event list for the given
    // month + day (1-indexed). Uses streamed parsing because the
    // raw response is ~200KB.
    bool fetchOnThisDay(int month, int day,
                        std::vector<TodayEvent>& out,
                        size_t maxItems = 30);

    // Set after a fetch* call returns false. "http 503" / "parse
    // failed" / "transport: connection refused" -- caller can either
    // surface to user or retry.
    const std::string& lastError() const { return lastErr_; }

private:
    bool doGet(const std::string& url, std::string& bodyOut);

    Transport*  t_;
    std::string ua_;
    std::string lastErr_;
};

namespace detail {
// Percent-encode for URL path/query. Pure ASCII letters/digits and
// `-_.~` pass through; everything else (spaces, parens, accents, etc)
// becomes %XX. Exposed for testing.
std::string urlEncode(const std::string& s);
} // namespace detail

} // namespace wiki
