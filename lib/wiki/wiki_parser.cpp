#include "wiki_parser.h"
#include <ArduinoJson.h>
#include <cstddef>

namespace wiki {

namespace detail {

std::string stripHtml(const std::string& s) {
    // MediaWiki snippets contain <span class="searchmatch">...</span>
    // wrappers and the occasional &quot;/&amp;. Single-pass: copy
    // chars, swallow tag bodies, decode a small set of named entities.
    std::string out;
    out.reserve(s.size());
    bool inTag = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inTag) {
            if (c == '>') inTag = false;
            continue;
        }
        if (c == '<') { inTag = true; continue; }
        if (c == '&') {
            // Smallest set that actually appears in MediaWiki snippets.
            if (s.compare(i, 5, "&amp;") == 0)   { out += '&'; i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0)  { out += '"'; i += 5; continue; }
            if (s.compare(i, 6, "&apos;") == 0)  { out += '\''; i += 5; continue; }
            if (s.compare(i, 4, "&lt;") == 0)    { out += '<'; i += 3; continue; }
            if (s.compare(i, 4, "&gt;") == 0)    { out += '>'; i += 3; continue; }
            if (s.compare(i, 6, "&nbsp;") == 0)  { out += ' '; i += 5; continue; }
        }
        out += c;
    }
    return out;
}

std::string slugFromUrl(const std::string& url) {
    // ".../wiki/Carl_Peters" -> "Carl_Peters". Tolerate trailing
    // fragments or query strings.
    auto wikiPos = url.find("/wiki/");
    if (wikiPos == std::string::npos) return {};
    auto start = wikiPos + 6;
    auto end = url.find_first_of("?#", start);
    if (end == std::string::npos) end = url.size();
    return url.substr(start, end - start);
}

} // namespace detail

bool parseSummary(const std::string& json, ArticleSummary& out) {
    // 4 KB is enough for the summary endpoint's structure. ArduinoJson
    // 7 grows the document automatically on the heap, so this is a
    // hint not a hard cap.
    JsonDocument doc;
    auto err = deserializeJson(doc, json);
    if (err) return false;
    if (!doc["titles"]["canonical"].is<const char*>()) return false;

    out.pageId       = doc["titles"]["canonical"] | "";
    out.title        = doc["title"] | "";
    out.description  = doc["description"] | "";
    out.extract      = doc["extract"] | "";
    out.extractHtml  = doc["extract_html"] | "";
    out.canonicalUrl = doc["content_urls"]["desktop"]["page"] | "";

    if (out.title.empty() && out.pageId.empty()) return false;
    return true;
}

bool parseMoreLike(const std::string& json,
                   std::vector<RelatedItem>& out,
                   size_t maxItems) {
    JsonDocument doc;
    auto err = deserializeJson(doc, json);
    if (err) return false;

    // ArduinoJson 7 proxy types aren't copyable; access via JsonVariantConst.
    JsonVariantConst search = doc["query"]["search"];
    if (search.isNull() || !search.is<JsonArrayConst>()) return false;

    out.clear();
    out.reserve(maxItems);
    for (JsonObjectConst item : search.as<JsonArrayConst>()) {
        if (out.size() >= maxItems) break;
        RelatedItem r;
        r.title   = item["title"]   | "";
        r.snippet = detail::stripHtml(item["snippet"] | "");
        // pageId is the title with spaces -> underscores. URL-safe.
        r.pageId  = r.title;
        for (auto& c : r.pageId) if (c == ' ') c = '_';
        if (!r.title.empty()) out.push_back(std::move(r));
    }
    return true;
}

bool parseOnThisDay(ByteReader& src,
                    std::vector<TodayEvent>& out,
                    size_t maxItems) {
    // Filter doc tells ArduinoJson which keys to retain. The array
    // wildcard `[0]` applies to every element in that array. Anything
    // not listed gets dropped as the stream is consumed, so even
    // though Wikipedia returns hundreds of KB the deserialized
    // document stays tiny.
    JsonDocument filter;
    filter["selected"][0]["text"]                     = true;
    filter["selected"][0]["year"]                     = true;
    filter["selected"][0]["pages"][0]["title"]        = true;
    filter["selected"][0]["pages"][0]["titles"]["canonical"] = true;

    JsonDocument doc;
    auto err = deserializeJson(doc, src,
                               DeserializationOption::Filter(filter));
    if (err) return false;

    JsonVariantConst selected = doc["selected"];
    if (selected.isNull() || !selected.is<JsonArrayConst>()) return false;

    out.clear();
    out.reserve(maxItems);
    for (JsonObjectConst ev : selected.as<JsonArrayConst>()) {
        if (out.size() >= maxItems) break;
        TodayEvent t;
        t.year = ev["year"] | 0;
        t.text = ev["text"] | "";

        JsonVariantConst pages = ev["pages"];
        if (pages.is<JsonArrayConst>()) {
            JsonArrayConst pArr = pages.as<JsonArrayConst>();
            if (pArr.size() > 0) {
                JsonObjectConst p0 = pArr[0];
                t.title  = p0["title"] | "";
                t.pageId = p0["titles"]["canonical"] | "";
            }
        }
        // Skip events with no linked page -- can't open them anyway.
        if (!t.pageId.empty()) out.push_back(std::move(t));
    }
    return true;
}

bool parseOpenSearch(const std::string& json,
                     std::vector<SearchHit>& out,
                     size_t maxItems) {
    // opensearch returns ["<query>", [titles], [descs], [urls]]. Four
    // arrays in a single JSON array -- unusual shape but stable.
    JsonDocument doc;
    auto err = deserializeJson(doc, json);
    if (err) return false;
    if (!doc.is<JsonArray>()) return false;

    JsonArrayConst root = doc.as<JsonArrayConst>();
    if (root.size() < 4) return false;

    JsonVariantConst titles = root[1];
    JsonVariantConst descs  = root[2];
    JsonVariantConst urls   = root[3];
    if (!titles.is<JsonArrayConst>() || !urls.is<JsonArrayConst>()) return false;

    JsonArrayConst tArr = titles.as<JsonArrayConst>();
    JsonArrayConst dArr = descs.is<JsonArrayConst>() ? descs.as<JsonArrayConst>() : JsonArrayConst();
    JsonArrayConst uArr = urls.as<JsonArrayConst>();

    out.clear();
    size_t n = tArr.size();
    if (n > uArr.size()) n = uArr.size();
    if (n > maxItems)    n = maxItems;
    out.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        SearchHit h;
        h.title       = tArr[i] | "";
        h.description = (i < dArr.size()) ? (dArr[i] | "") : std::string{};
        h.pageId      = detail::slugFromUrl(uArr[i] | "");
        if (!h.title.empty()) out.push_back(std::move(h));
    }
    return true;
}

} // namespace wiki
