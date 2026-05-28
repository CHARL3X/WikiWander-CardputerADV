#include "wiki_client.h"
#include "wiki_parser.h"

namespace wiki {

namespace {
constexpr const char* kHostREST = "https://en.wikipedia.org/api/rest_v1";
constexpr const char* kHostAPI  = "https://en.wikipedia.org/w/api.php";
} // namespace

namespace detail {

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

} // namespace detail

WikiClient::WikiClient(Transport* transport, std::string userAgent)
    : t_(transport), ua_(std::move(userAgent)) {}

bool WikiClient::doGet(const std::string& url, std::string& bodyOut) {
    if (!t_) {
        lastErr_ = "no transport";
        return false;
    }
    HttpResult r = t_->get(url, ua_);
    if (!r.error.empty()) {
        lastErr_ = "transport: " + r.error;
        return false;
    }
    if (r.status < 200 || r.status >= 300) {
        lastErr_ = "http " + std::to_string(r.status);
        return false;
    }
    bodyOut = std::move(r.body);
    lastErr_.clear();
    return true;
}

// Pull the JSON-escaped "text" value out of an action=parse response.
// Minimal parser: walks until "text":"...", then collects bytes until
// the matching unescaped quote. Unescapes \" \\ \/ \n \t \uXXXX
// sequences inline.
static bool extractParseText(const std::string& json, std::string& out) {
    auto key = json.find("\"text\":\"");
    if (key == std::string::npos) return false;
    out.clear();
    out.reserve(json.size() / 2);
    for (size_t i = key + 8; i < json.size(); ++i) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            char nx = json[i + 1];
            if      (nx == '"')  { out += '"';  ++i; }
            else if (nx == '\\') { out += '\\'; ++i; }
            else if (nx == '/')  { out += '/';  ++i; }
            else if (nx == 'n')  { out += '\n'; ++i; }
            else if (nx == 't')  { out += '\t'; ++i; }
            else if (nx == 'r')  { out += '\r'; ++i; }
            else if (nx == 'u' && i + 5 < json.size()) {
                // \uXXXX -- decode to UTF-8
                unsigned long code = std::strtoul(json.substr(i + 2, 4).c_str(),
                                                  nullptr, 16);
                if (code < 0x80) {
                    out += static_cast<char>(code);
                } else if (code < 0x800) {
                    out += static_cast<char>(0xC0 | (code >> 6));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (code >> 12));
                    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                }
                i += 5;
            } else { out += json[i]; }
        } else if (c == '"') {
            return true;
        } else {
            out += c;
        }
    }
    return false;
}

bool WikiClient::fetchArticleLeadHtml(const std::string& pageId,
                                      std::string& htmlOut) {
    // action=parse with section=0 returns just the lead. prop=text
    // gives the rendered HTML (with inline <a> tags preserved).
    // formatversion=2 makes the response shape predictable.
    std::string url = std::string(kHostAPI)
        + "?action=parse&page=" + detail::urlEncode(pageId)
        + "&section=0&prop=text&format=json&formatversion=2&disabletoc=1";
    std::string body;
    if (!doGet(url, body)) return false;
    if (!extractParseText(body, htmlOut)) {
        lastErr_ = "parse failed (lead html)";
        return false;
    }
    return true;
}

// Helper: after a summary fetch succeeds, attempt to enrich it with
// the lead-section HTML so the article reader can highlight inline
// links. If the second fetch fails we leave extractHtml as the
// summary's (link-stripped) version -- not a hard error since the
// reader falls back to plain text rendering gracefully.
void WikiClient_enrichWithLeadHtml(WikiClient* self, ArticleSummary& a) {
    if (a.pageId.empty()) return;
    std::string rich;
    if (self->fetchArticleLeadHtml(a.pageId, rich) && !rich.empty()) {
        a.extractHtml = std::move(rich);
    }
}

bool WikiClient::fetchRandom(ArticleSummary& out) {
    std::string url = std::string(kHostREST) + "/page/random/summary";
    std::string body;
    if (!doGet(url, body)) return false;
    if (!parseSummary(body, out)) {
        lastErr_ = "parse failed (random summary)";
        return false;
    }
    WikiClient_enrichWithLeadHtml(this, out);
    return true;
}

bool WikiClient::fetchByPageId(const std::string& pageId, ArticleSummary& out) {
    // pageId is already in URL-safe form (Wikipedia canonical: spaces
    // are underscores). Encode anyway for safety against non-ASCII.
    std::string url = std::string(kHostREST) + "/page/summary/"
                    + detail::urlEncode(pageId);
    std::string body;
    if (!doGet(url, body)) return false;
    if (!parseSummary(body, out)) {
        lastErr_ = "parse failed (summary)";
        return false;
    }
    WikiClient_enrichWithLeadHtml(this, out);
    return true;
}

bool WikiClient::fetchMoreLike(const std::string& pageId,
                               std::vector<RelatedItem>& out,
                               size_t maxItems) {
    // pageId is the canonical title (underscored). morelike: takes
    // the title as a search term -- spaces are fine, no special
    // syntax needed inside the value.
    std::string url = std::string(kHostAPI)
        + "?action=query&list=search&srsearch=morelike:"
        + detail::urlEncode(pageId)
        + "&srlimit=" + std::to_string(maxItems)
        + "&srprop=snippet&format=json&formatversion=2";
    std::string body;
    if (!doGet(url, body)) return false;
    if (!parseMoreLike(body, out, maxItems)) {
        lastErr_ = "parse failed (morelike)";
        return false;
    }
    return true;
}

bool WikiClient::fetchOnThisDay(int month, int day,
                                std::vector<TodayEvent>& out,
                                size_t maxItems) {
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        lastErr_ = "bad date";
        return false;
    }
    char dateBuf[8];
    snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d", month, day);
    std::string url = std::string(kHostREST) + "/feed/onthisday/selected/" + dateBuf;

    if (!t_) { lastErr_ = "no transport"; return false; }

    bool parseOk = false;
    HttpResult r = t_->getStreamed(url, ua_, [&](ByteReader& body) {
        parseOk = parseOnThisDay(body, out, maxItems);
    });
    if (!r.error.empty()) {
        lastErr_ = "transport: " + r.error;
        return false;
    }
    if (r.status < 200 || r.status >= 300) {
        lastErr_ = "http " + std::to_string(r.status);
        return false;
    }
    if (!parseOk) {
        lastErr_ = "parse failed (onthisday)";
        return false;
    }
    lastErr_.clear();
    return true;
}

bool WikiClient::search(const std::string& query,
                        std::vector<SearchHit>& out,
                        size_t maxItems) {
    std::string url = std::string(kHostAPI)
        + "?action=opensearch&search=" + detail::urlEncode(query)
        + "&limit=" + std::to_string(maxItems)
        + "&format=json";
    std::string body;
    if (!doGet(url, body)) return false;
    if (!parseOpenSearch(body, out, maxItems)) {
        lastErr_ = "parse failed (opensearch)";
        return false;
    }
    return true;
}

} // namespace wiki
