#include "extract_html.h"

#include <cstddef>
#include <cstdint>

namespace wiki {

namespace {

// Element names whose ENTIRE contents we drop (start tag, body,
// end tag). These show up in Wikipedia's action=parse output and
// are noise we never want to render:
//   style, script    -- CSS / JS templates
//   sup              -- citation markers like [1]
//   table            -- infoboxes and data tables
//   figure           -- image figures + captions
//   div.shortdescription, div.hatnote -- header annotations that
//   duplicate or distract from the body text
constexpr const char* kDropElements[] = {
    "style", "script", "sup", "table", "figure",
};

// Returns true if `s[from..]` starts with `<name`, where the char
// after `name` is whitespace or `>` (so we don't false-match
// `<scripted>` for `<script>`).
bool tagOpens(const std::string& s, size_t from, const char* name) {
    size_t nameLen = 0;
    while (name[nameLen]) ++nameLen;
    if (from + 1 + nameLen >= s.size()) return false;
    if (s[from] != '<') return false;
    for (size_t k = 0; k < nameLen; ++k) {
        char a = s[from + 1 + k];
        if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
        if (a != name[k]) return false;
    }
    char after = s[from + 1 + nameLen];
    return (after == ' ' || after == '\t' || after == '>' || after == '/');
}

// Match `</name>` starting at `pos`. Distinct from tagOpens (which
// matches `<name`) because the closer's structure is different.
bool tagCloses(const std::string& s, size_t pos, const char* name) {
    size_t nameLen = 0;
    while (name[nameLen]) ++nameLen;
    if (pos + 2 + nameLen >= s.size()) return false;
    if (s[pos] != '<' || s[pos + 1] != '/') return false;
    for (size_t k = 0; k < nameLen; ++k) {
        char a = s[pos + 2 + k];
        if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
        if (a != name[k]) return false;
    }
    char after = s[pos + 2 + nameLen];
    return (after == ' ' || after == '\t' || after == '>');
}

// Find the matching close tag `</name>` from `from`, accounting for
// nesting depth. Returns the index of the '<' of the matching closer,
// or std::string::npos if not found.
size_t findMatchingClose(const std::string& s, size_t from, const char* name) {
    int depth = 1;
    size_t i = from;
    while (i < s.size()) {
        if (s[i] == '<') {
            if (tagCloses(s, i, name)) {
                --depth;
                if (depth == 0) return i;
                size_t gt = s.find('>', i);
                if (gt == std::string::npos) return std::string::npos;
                i = gt + 1;
                continue;
            }
            if (tagOpens(s, i, name)) {
                ++depth;
                size_t gt = s.find('>', i);
                if (gt == std::string::npos) return std::string::npos;
                i = gt + 1;
                continue;
            }
        }
        ++i;
    }
    return std::string::npos;
}

// Append a decoded character / glyph for an HTML entity into `out`.
// Pos is the index of '&'; returns the index just past the closing
// ';' if recognized, else returns pos+1 with no append (caller will
// write the original '&' verbatim).
size_t decodeEntity(const std::string& s, size_t pos, std::string& out) {
    // Numeric: &#NNN; or &#xHH;
    if (pos + 2 < s.size() && s[pos + 1] == '#') {
        size_t i = pos + 2;
        int base = 10;
        if (i < s.size() && (s[i] == 'x' || s[i] == 'X')) { base = 16; ++i; }
        long code = 0;
        size_t start = i;
        while (i < s.size() && s[i] != ';') {
            char c = s[i];
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            code = code * base + d;
            ++i;
        }
        if (i > start && i < s.size() && s[i] == ';' && code > 0) {
            // ASCII -> direct emit. Non-ASCII codepoints we
            // translate to UTF-8 so the existing toAscii() pass in
            // the renderer can deal with them downstream.
            if (code < 0x80) {
                out += static_cast<char>(code);
            } else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                // 4-byte UTF-8 -- emit '?' instead; we don't render
                // these on the bitmap fonts.
                out += '?';
            }
            return i + 1;
        }
        return pos + 1;  // give up; caller will append '&' itself
    }

    // Named entities (a tiny subset covering what Wikipedia actually
    // emits inline). MediaWiki normalizes most entities to UTF-8
    // before serving, so we only need the safety net for the few
    // that survive.
    struct Named { const char* name; const char* repl; };
    static const Named table[] = {
        {"amp;",   "&"},
        {"quot;",  "\""},
        {"apos;",  "'"},
        {"lt;",    "<"},
        {"gt;",    ">"},
        {"nbsp;",  " "},
        {"mdash;", "\xE2\x80\x94"},  // U+2014 em dash, UTF-8
        {"ndash;", "\xE2\x80\x93"},  // U+2013 en dash, UTF-8
        {"hellip;","\xE2\x80\xA6"},  // U+2026 ellipsis
        {"laquo;", "\xC2\xAB"},
        {"raquo;", "\xC2\xBB"},
    };
    for (auto& n : table) {
        size_t nlen = 0;
        while (n.name[nlen]) ++nlen;
        if (pos + 1 + nlen <= s.size() &&
            s.compare(pos + 1, nlen, n.name) == 0) {
            out += n.repl;
            return pos + 1 + nlen;
        }
    }
    return pos + 1;
}

// Read attribute value out of a tag. Handles single- and double-
// quoted forms. `nameStart` is the index inside the tag where the
// attribute name begins; `tagEnd` is one past the closing '>'.
// Returns empty if attr not found.
std::string readAttr(const std::string& s, size_t openLT, size_t tagEnd,
                     const char* name) {
    size_t nameLen = 0;
    while (name[nameLen]) ++nameLen;
    for (size_t i = openLT + 1; i + nameLen < tagEnd; ++i) {
        // Match name=  (case-insensitive)
        bool ok = true;
        for (size_t k = 0; k < nameLen; ++k) {
            char a = s[i + k];
            char b = name[k];
            if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
            if (a != b) { ok = false; break; }
        }
        if (!ok) continue;
        size_t j = i + nameLen;
        if (j >= tagEnd || s[j] != '=') continue;
        ++j;
        if (j >= tagEnd) return {};
        char quote = s[j];
        if (quote != '"' && quote != '\'') return {};
        ++j;
        size_t valStart = j;
        while (j < tagEnd && s[j] != quote) ++j;
        return s.substr(valStart, j - valStart);
    }
    return {};
}

// Pull the wiki pageId out of an href. Returns empty if href isn't
// a usable internal link.
//   /wiki/Hypertext              -> "Hypertext"
//   /wiki/Hyperion_(novel)#title -> "Hyperion_(novel)"
//   /wiki/Special:RandomPage     -> "" (namespace prefix rejected)
//   https://en.wikipedia.org/... -> "" (external, treat as plain text)
std::string pageIdFromHref(const std::string& href) {
    constexpr const char* kPrefix = "/wiki/";
    constexpr size_t      kPrefixLen = 6;
    if (href.size() < kPrefixLen) return {};
    if (href.compare(0, kPrefixLen, kPrefix) != 0) return {};
    std::string tail = href.substr(kPrefixLen);
    // Cut off fragment and query
    auto cut = tail.find_first_of("?#");
    if (cut != std::string::npos) tail.resize(cut);
    // Reject namespace-prefixed targets (Special:, File:, etc.)
    // -- they don't render via /page/summary/. Heuristic: ':' in
    // the slug. We also reject empty slugs.
    if (tail.empty()) return {};
    if (tail.find(':') != std::string::npos) return {};
    return tail;
}

// Find matching closing tag of a given name starting after `from`.
// Returns std::string::npos if not found.
size_t findClosingTag(const std::string& s, const char* tagName, size_t from) {
    size_t nameLen = 0;
    while (tagName[nameLen]) ++nameLen;
    for (size_t i = from; i + nameLen + 2 < s.size(); ++i) {
        if (s[i] == '<' && s[i + 1] == '/') {
            bool match = true;
            for (size_t k = 0; k < nameLen; ++k) {
                char a = s[i + 2 + k];
                if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
                if (a != tagName[k]) { match = false; break; }
            }
            if (!match) continue;
            // Ensure the following char is '>' or whitespace
            char after = s[i + 2 + nameLen];
            if (after != '>' && after != ' ' && after != '\t') continue;
            return i;
        }
    }
    return std::string::npos;
}

// Decode a slice of HTML body into plain text, expanding entities.
// Strips any further tags encountered inside the slice (defensive --
// we expect callers to pass clean text-only slices). Appends into
// `out`.
void appendDecoded(const std::string& s, size_t from, size_t to,
                   std::string& out) {
    size_t i = from;
    while (i < to) {
        char c = s[i];
        if (c == '&') {
            size_t after = decodeEntity(s, i, out);
            if (after == i + 1) {
                // entity not recognized -- emit '&' verbatim
                out += '&';
                ++i;
            } else {
                i = after;
            }
        } else if (c == '<') {
            // Skip the embedded tag entirely (we don't handle nested
            // <a>; opening another <a> inside an <a> is invalid HTML
            // and Wikipedia doesn't emit it).
            size_t gt = s.find('>', i);
            if (gt == std::string::npos || gt >= to) break;
            i = gt + 1;
        } else {
            out += c;
            ++i;
        }
    }
}

} // namespace

std::vector<ExtractToken> tokenizeExtractHtml(const std::string& html) {
    std::vector<ExtractToken> out;
    if (html.empty()) return out;

    std::string pending;  // accumulating plain text not yet emitted

    auto flushText = [&]() {
        if (pending.empty()) return;
        ExtractToken t;
        t.kind = ExtractToken::Text;
        t.text = std::move(pending);
        pending.clear();
        out.push_back(std::move(t));
    };

    size_t i = 0;
    while (i < html.size()) {
        char c = html[i];
        if (c == '<') {
            // Check if this is an <a ...> tag (case-insensitive).
            // We treat everything else as a tag to strip.
            bool isA = false;
            if (i + 2 < html.size()) {
                char c1 = html[i + 1];
                char c2 = html[i + 2];
                if ((c1 == 'a' || c1 == 'A') &&
                    (c2 == ' ' || c2 == '\t' || c2 == '>')) {
                    isA = true;
                }
            }
            if (!isA) {
                // First check the drop-element list: <style>, <sup>,
                // <table>, etc. need their entire contents discarded,
                // not just the tag stripped.
                bool dropped = false;
                for (auto& dropName : kDropElements) {
                    if (!tagOpens(html, i, dropName)) continue;
                    // Skip to '>' to consume the opener
                    size_t openEnd = html.find('>', i);
                    if (openEnd == std::string::npos) { i = html.size(); break; }
                    // Self-closing form (rare): skip nothing more
                    if (openEnd > 0 && html[openEnd - 1] == '/') {
                        i = openEnd + 1;
                    } else {
                        // Find the matching closer at the same depth
                        size_t closeLt = findMatchingClose(html, openEnd + 1, dropName);
                        if (closeLt == std::string::npos) { i = html.size(); break; }
                        size_t closeGt = html.find('>', closeLt);
                        if (closeGt == std::string::npos) { i = html.size(); break; }
                        i = closeGt + 1;
                    }
                    dropped = true;
                    break;
                }
                if (dropped) continue;

                // Otherwise: strip just this one tag (keep inner
                // text); applies to <p>, <div>, <span>, <i>, <b>,
                // closing tags, comments, etc.
                size_t gt = html.find('>', i);
                if (gt == std::string::npos) { i = html.size(); break; }
                i = gt + 1;
                continue;
            }

            // It IS an <a ...>. Find the tag end and the matching </a>.
            size_t tagEnd = html.find('>', i);
            if (tagEnd == std::string::npos) { i = html.size(); break; }
            ++tagEnd;  // one past '>'
            size_t closeOpen = findClosingTag(html, "a", tagEnd);
            if (closeOpen == std::string::npos) {
                // unbalanced -- treat as plain by skipping the
                // opening tag and continuing
                i = tagEnd;
                continue;
            }
            std::string href = readAttr(html, i, tagEnd, "href");
            std::string pageId = pageIdFromHref(href);

            // Decode the inner text of the <a>
            std::string linkText;
            appendDecoded(html, tagEnd, closeOpen, linkText);

            // Advance past the </a>
            size_t afterClose = html.find('>', closeOpen);
            if (afterClose == std::string::npos) afterClose = html.size() - 1;
            i = afterClose + 1;

            if (linkText.empty()) continue;  // shouldn't happen, but safe

            if (pageId.empty()) {
                // Not a wiki-internal link -- fold its text into the
                // pending plain-text buffer.
                pending += linkText;
                continue;
            }

            // Emit accumulated text, then a Link token.
            flushText();
            ExtractToken t;
            t.kind   = ExtractToken::Link;
            t.text   = std::move(linkText);
            t.pageId = std::move(pageId);
            out.push_back(std::move(t));
            continue;
        }

        if (c == '&') {
            size_t after = decodeEntity(html, i, pending);
            if (after == i + 1) {
                pending += '&';
                ++i;
            } else {
                i = after;
            }
            continue;
        }

        pending += c;
        ++i;
    }

    flushText();
    return out;
}

} // namespace wiki
