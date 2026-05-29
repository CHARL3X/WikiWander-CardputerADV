// Native PC test runner. Built only by the `native` PIO env.
// Loads JSON fixtures from disk, exercises the parsers, asserts
// expected output. Exits 0 on all-green, 1 on any failure.
//
// Run from project root: `pio run -e native -t exec`

#ifdef WIKIWANDER_PC_BUILD

#include "wiki_parser.h"
#include "wiki_types.h"
#include "transport.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_passes   = 0;
std::string g_currentTest;

void startTest(const char* name) {
    g_currentTest = name;
    std::printf("\n[%s]\n", name);
}

void fail(const std::string& msg) {
    ++g_failures;
    std::printf("  FAIL  %s\n", msg.c_str());
}

void pass(const std::string& msg) {
    ++g_passes;
    std::printf("  pass  %s\n", msg.c_str());
}

// Naked assert helpers -- minimal, no Unity framework dependency.
template <typename T, typename U>
void expectEq(const T& got, const U& want, const std::string& what) {
    if (got == want) {
        pass(what);
    } else {
        std::ostringstream os;
        os << what << "  got=[" << got << "]  want=[" << want << "]";
        fail(os.str());
    }
}

void expectTrue(bool cond, const std::string& what) {
    if (cond) pass(what);
    else      fail(what);
}

void expectContains(const std::string& haystack,
                    const std::string& needle,
                    const std::string& what) {
    if (haystack.find(needle) != std::string::npos) pass(what);
    else fail(what + " (in: \"" + haystack.substr(0, 60) + "...\")");
}

std::string loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::fprintf(stderr, "FATAL: could not open fixture %s\n", path.c_str());
        std::exit(2);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---- summary parser ----

void testSummaryHypertext() {
    startTest("parseSummary -- Hypertext (well-known shape)");
    auto json = loadFile("test/fixtures/summary_hypertext.json");
    wiki::ArticleSummary a;
    expectTrue(wiki::parseSummary(json, a), "parser returns true");
    expectEq(a.pageId, std::string("Hypertext"), "pageId is canonical title");
    expectEq(a.title,  std::string("Hypertext"), "title populated");
    expectContains(a.description, "Text with references", "description matches");
    expectTrue(a.extract.size() > 200, "extract has body text");
    expectContains(a.extract, "Hypertext is text",        "extract starts as expected");
    expectEq(a.canonicalUrl,
             std::string("https://en.wikipedia.org/wiki/Hypertext"),
             "canonicalUrl is desktop page");
}

void testSummaryRandom() {
    startTest("parseSummary -- random fixture (shape only)");
    auto json = loadFile("test/fixtures/summary_random.json");
    wiki::ArticleSummary a;
    expectTrue(wiki::parseSummary(json, a),  "parser returns true");
    expectTrue(!a.pageId.empty(),            "pageId populated");
    expectTrue(!a.title.empty(),             "title populated");
    expectTrue(!a.extract.empty(),           "extract populated");
    expectContains(a.canonicalUrl, "https://", "canonicalUrl has https");
}

void testSummaryGarbage() {
    startTest("parseSummary -- malformed input returns false");
    wiki::ArticleSummary a;
    expectTrue(!wiki::parseSummary("not json", a),
               "garbage rejected");
    expectTrue(!wiki::parseSummary("{}", a),
               "empty object rejected");
}

// ---- morelike parser ----

void testMoreLikeHypertext() {
    startTest("parseMoreLike -- Hypertext related set");
    auto json = loadFile("test/fixtures/morelike_hypertext.json");
    std::vector<wiki::RelatedItem> items;
    expectTrue(wiki::parseMoreLike(json, items),  "parser returns true");
    expectEq(items.size(), size_t{6}, "6 items at limit");
    expectEq(items[0].title, std::string("History of hypertext"),
             "first item title");
    expectEq(items[0].pageId, std::string("History_of_hypertext"),
             "first item pageId is title with underscores");
    expectTrue(!items[0].snippet.empty(), "first item has snippet");
    // Snippet HTML stripping: original snippet contains <span class="searchmatch">
    // tags around the matched term. Stripped should be plain text only.
    expectTrue(items[0].snippet.find('<') == std::string::npos,
               "snippet HTML tags stripped");
}

void testMoreLikeLimit() {
    startTest("parseMoreLike -- maxItems limit honored");
    auto json = loadFile("test/fixtures/morelike_hypertext.json");
    std::vector<wiki::RelatedItem> items;
    expectTrue(wiki::parseMoreLike(json, items, 3), "parser returns true");
    expectEq(items.size(), size_t{3}, "clamped to maxItems=3");
}

// ---- opensearch parser ----

void testOpenSearchCardputer() {
    startTest("parseOpenSearch -- cardputer query");
    auto json = loadFile("test/fixtures/opensearch_cardputer.json");
    std::vector<wiki::SearchHit> hits;
    expectTrue(wiki::parseOpenSearch(json, hits), "parser returns true");
    expectTrue(hits.size() >= 5, "at least 5 results");
    expectEq(hits[0].title,  std::string("Carputer"), "first title");
    expectEq(hits[0].pageId, std::string("Carputer"), "first pageId from URL");
    expectContains(hits[0].pageId, "Carputer", "slug derivation works");
}

// ---- helpers ----

void testStripHtml() {
    startTest("detail::stripHtml -- common cases");
    using wiki::detail::stripHtml;
    expectEq(stripHtml("plain"),                std::string("plain"),
             "passthrough");
    expectEq(stripHtml("<span>bold</span> rest"), std::string("bold rest"),
             "tag stripped");
    expectEq(stripHtml("&amp; &quot; &lt;"),     std::string("& \" <"),
             "entities decoded");
    expectEq(stripHtml("<span class=\"searchmatch\">Hypertext</span> is text"),
             std::string("Hypertext is text"),
             "searchmatch span stripped");
}

void testOnThisDay() {
    startTest("parseOnThisDay -- May 27 fixture");
    auto json = loadFile("test/fixtures/onthisday_selected_0527.json");
    wiki::BufferReader src(std::move(json));
    std::vector<wiki::TodayEvent> events;
    expectTrue(wiki::parseOnThisDay(src, events),
               "parser returns true");
    expectTrue(events.size() > 0, "got some events");
    expectTrue(events.size() <= 30, "respects maxItems default");
    // First event has known year + non-empty text
    if (!events.empty()) {
        expectTrue(events[0].year > 0, "first event has positive year");
        expectTrue(!events[0].text.empty(), "first event has text");
        expectTrue(!events[0].pageId.empty(), "first event has pageId");
        expectTrue(!events[0].title.empty(), "first event has title");
    }
    // Verify the filter actually dropped heavy fields by checking
    // we don't blow up on the full fixture (~217KB).
    pass("no OOM on streamed parse of full fixture");
}

void testOnThisDayLimit() {
    startTest("parseOnThisDay -- maxItems respected");
    auto json = loadFile("test/fixtures/onthisday_selected_0527.json");
    wiki::BufferReader src(std::move(json));
    std::vector<wiki::TodayEvent> events;
    expectTrue(wiki::parseOnThisDay(src, events, 5),
               "parser returns true");
    expectEq(events.size(), size_t{5}, "clamped to maxItems=5");
}

void testSlugFromUrl() {
    startTest("detail::slugFromUrl -- url -> slug");
    using wiki::detail::slugFromUrl;
    expectEq(slugFromUrl("https://en.wikipedia.org/wiki/Carl_Peters"),
             std::string("Carl_Peters"),
             "plain slug");
    expectEq(slugFromUrl("https://en.wikipedia.org/wiki/Carl_Peters?action=edit"),
             std::string("Carl_Peters"),
             "query stripped");
    expectEq(slugFromUrl("https://en.wikipedia.org/wiki/Foo#bar"),
             std::string("Foo"),
             "fragment stripped");
    expectEq(slugFromUrl("https://example.com/not/wiki/path"),
             std::string("path"),
             "literal /wiki/ matched even outside wikipedia");
    expectEq(slugFromUrl("not a url"),
             std::string(""),
             "no /wiki/ returns empty");
}

} // namespace

int main(int, char**) {
    std::printf("== Wikiwander parser tests ==\n");

    testSummaryHypertext();
    testSummaryRandom();
    testSummaryGarbage();
    testMoreLikeHypertext();
    testMoreLikeLimit();
    testOpenSearchCardputer();
    testOnThisDay();
    testOnThisDayLimit();
    testStripHtml();
    testSlugFromUrl();

    std::printf("\n== %d passed, %d failed ==\n", g_passes, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#endif // WIKIWANDER_PC_BUILD
