// Tokenizer for Wikipedia's `extract_html` field. Splits a chunk of
// summary HTML into a flat list of (text, link) segments so the
// article reader can highlight + follow internal wiki links.
//
// Pure C++; no Arduino dependencies. Reusable by PC tests and the
// device renderer alike.
//
// Scope is intentionally minimal:
//   - Only `<a href="...">linktext</a>` is interpreted.
//   - Other tags (i, b, sup, span, etc.) are *stripped* -- we keep
//     their inner text, drop the markup.
//   - Common HTML entities (&amp;, &quot;, &lt;, &gt;, &nbsp;,
//     &#39;, &mdash;, &ndash;) get decoded inline.
//   - Internal wiki links are kept only when href is a `/wiki/...`
//     URL without a namespace prefix (Special:, File:, etc.). The
//     pageId stripped from the URL is exposed for the caller.
#pragma once

#include <string>
#include <vector>

namespace wiki {

struct ExtractToken {
    enum Kind { Text, Link };
    Kind        kind;
    std::string text;     // visible text (entities already decoded)
    std::string pageId;   // link target slug, only set for Kind::Link
};

// Tokenize an extract_html chunk. Always succeeds; on malformed
// input we drop unrecognized tags and keep moving. Returns an empty
// vector iff the input was empty.
std::vector<ExtractToken> tokenizeExtractHtml(const std::string& html);

} // namespace wiki
