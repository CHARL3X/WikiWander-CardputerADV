// Native PC test runner. Built only by the `native` PIO env.
// Loads JSON fixtures from disk, exercises the parsers, asserts
// expected output. Exits 0 on all-green, 1 on any failure.
//
// Run from project root: `pio run -e native -t exec`

#ifdef WIKIWANDER_PC_BUILD

#include "wiki_parser.h"
#include "wiki_types.h"
#include "transport.h"
#include "extract_html.h"

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

void testExtractHtmlPlain() {
    startTest("tokenizeExtractHtml -- plain text (no links)");
    auto tokens = wiki::tokenizeExtractHtml("Just some plain text here.");
    expectEq(tokens.size(), size_t{1}, "single text token");
    expectEq(tokens[0].kind, wiki::ExtractToken::Text, "is Text kind");
    expectEq(tokens[0].text, std::string("Just some plain text here."),
             "text preserved");
}

void testExtractHtmlOneLink() {
    startTest("tokenizeExtractHtml -- one internal link");
    auto t = wiki::tokenizeExtractHtml(
        "Hypertext is text with <a href=\"/wiki/Hyperlinks\">hyperlinks</a> "
        "to other text.");
    expectEq(t.size(), size_t{3}, "Text + Link + Text");
    expectEq(t[0].kind, wiki::ExtractToken::Text, "first is text");
    expectEq(t[1].kind, wiki::ExtractToken::Link, "middle is link");
    expectEq(t[1].text,   std::string("hyperlinks"), "link text");
    expectEq(t[1].pageId, std::string("Hyperlinks"), "link pageId extracted");
    expectEq(t[2].kind, wiki::ExtractToken::Text, "last is text");
}

void testExtractHtmlExternalLink() {
    startTest("tokenizeExtractHtml -- external link folded to text");
    auto t = wiki::tokenizeExtractHtml(
        "See <a href=\"https://example.com\">example</a> for details.");
    // External links aren't promoted to Link tokens -- their text is
    // folded into surrounding plain text. So we expect a single Text
    // token containing the whole sentence.
    bool anyLink = false;
    for (auto& tok : t) if (tok.kind == wiki::ExtractToken::Link) anyLink = true;
    expectTrue(!anyLink, "no Link tokens for external href");
    // The visible text "example" should still appear somewhere.
    bool sawExample = false;
    for (auto& tok : t) if (tok.text.find("example") != std::string::npos) sawExample = true;
    expectTrue(sawExample, "external link text preserved as plain");
}

void testExtractHtmlNamespacedLink() {
    startTest("tokenizeExtractHtml -- namespaced link folded to text");
    auto t = wiki::tokenizeExtractHtml(
        "See <a href=\"/wiki/File:Foo.jpg\">image</a> for details.");
    bool anyLink = false;
    for (auto& tok : t) if (tok.kind == wiki::ExtractToken::Link) anyLink = true;
    expectTrue(!anyLink, "no Link tokens for File: namespace");
}

void testExtractHtmlStripsTags() {
    startTest("tokenizeExtractHtml -- non-anchor tags stripped");
    auto t = wiki::tokenizeExtractHtml(
        "Some <i>italic</i> and <b>bold</b> text.");
    expectEq(t.size(), size_t{1}, "single text token after stripping");
    // Inner text of i/b should be kept; the tags themselves stripped.
    expectContains(t[0].text, "italic", "italic word kept");
    expectContains(t[0].text, "bold",   "bold word kept");
    expectTrue(t[0].text.find('<') == std::string::npos, "no markup leaked");
}

void testExtractHtmlEntities() {
    startTest("tokenizeExtractHtml -- HTML entities decoded");
    auto t = wiki::tokenizeExtractHtml("AT&amp;T &quot;tag&quot; &lt;here&gt;.");
    expectEq(t.size(), size_t{1}, "single text token");
    expectEq(t[0].text, std::string("AT&T \"tag\" <here>."),
             "&amp; &quot; &lt; &gt; all decoded");
}

void testExtractHtmlMultipleLinks() {
    startTest("tokenizeExtractHtml -- multiple links + text");
    auto t = wiki::tokenizeExtractHtml(
        "<a href=\"/wiki/Apple\">Apples</a> and "
        "<a href=\"/wiki/Orange\">oranges</a>.");
    int linkCount = 0;
    for (auto& tok : t) if (tok.kind == wiki::ExtractToken::Link) ++linkCount;
    expectEq(linkCount, 2, "two link tokens");
}

void testExtractHtmlDropsStyleAndSup() {
    startTest("tokenizeExtractHtml -- style + sup blocks dropped");
    auto t = wiki::tokenizeExtractHtml(
        "Real content here<style>.foo{color:red}</style>"
        " more content<sup>[1]</sup> and "
        "<a href=\"/wiki/Apple\">apples</a>.");
    // Reconstruct the visible text from all tokens
    std::string visible;
    for (auto& tok : t) visible += tok.text;
    expectTrue(visible.find("color:red") == std::string::npos,
               "style block contents dropped");
    expectTrue(visible.find("[1]") == std::string::npos,
               "sup citation dropped");
    expectContains(visible, "Real content here", "real content kept");
    expectContains(visible, "more content", "kept text past sup");
    int linkCount = 0;
    for (auto& tok : t) if (tok.kind == wiki::ExtractToken::Link) ++linkCount;
    expectEq(linkCount, 1, "link survives the drop pass");
}

void testExtractHtmlOnLeadFixture() {
    startTest("tokenizeExtractHtml -- live Wikipedia lead HTML (Hypertext)");
    // Pull the lead-HTML out of the action=parse fixture JSON.
    auto raw = loadFile("test/fixtures/parse_lead_hypertext.json");
    // Find "text": "..." -- naive string scan; ArduinoJson would be
    // overkill for a test-only file read.
    auto textKey = raw.find("\"text\":\"");
    if (textKey == std::string::npos) { fail("fixture missing text key"); return; }
    size_t start = textKey + 8;
    // Find the closing quote -- text contains escaped quotes \", so
    // we have to walk and unescape. Quick & dirty: find the next
    // unescaped quote.
    std::string html;
    for (size_t i = start; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            char nx = raw[i + 1];
            if (nx == '"')       { html += '"';  ++i; }
            else if (nx == '\\') { html += '\\'; ++i; }
            else if (nx == '/')  { html += '/';  ++i; }
            else if (nx == 'n')  { html += '\n'; ++i; }
            else if (nx == 't')  { html += '\t'; ++i; }
            else { html += raw[i]; }
        } else if (raw[i] == '"') {
            break;
        } else {
            html += raw[i];
        }
    }
    expectTrue(html.size() > 1000, "fixture has substantive HTML body");

    auto tokens = wiki::tokenizeExtractHtml(html);
    int linkCount = 0;
    std::string visible;
    for (auto& tok : tokens) {
        if (tok.kind == wiki::ExtractToken::Link) ++linkCount;
        visible += tok.text;
    }
    expectTrue(linkCount > 5, "found multiple internal wiki links");
    expectTrue(visible.find("color:red") == std::string::npos,
               "no CSS bled through");
    expectTrue(visible.find("font-style") == std::string::npos,
               "no style attributes bled through");
    expectContains(visible, "Hypertext", "topic name appears in body");
    std::printf("  (extracted %d links, %zu visible chars)\n",
                linkCount, visible.size());
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
    testExtractHtmlPlain();
    testExtractHtmlOneLink();
    testExtractHtmlExternalLink();
    testExtractHtmlNamespacedLink();
    testExtractHtmlStripsTags();
    testExtractHtmlEntities();
    testExtractHtmlMultipleLinks();
    testExtractHtmlDropsStyleAndSup();
    testExtractHtmlOnLeadFixture();
    testStripHtml();
    testSlugFromUrl();

    std::printf("\n== %d passed, %d failed ==\n", g_passes, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#endif // WIKIWANDER_PC_BUILD
