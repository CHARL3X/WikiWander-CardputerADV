// Shared text utilities for the screen layer. Word-wrap, ASCII
// sanitize, ellipsize. Independent of any specific screen so the
// paginator + search results + saved list can all share them.
#pragma once
#include <Arduino.h>
#include <M5GFX.h>
#include <vector>
#include "../../lib/wiki/extract_html.h"

namespace ui {

// Replace any byte > 0x7F with '?'. The Cardputer's bitmap fonts are
// 7-bit; non-ASCII reads as garbage box characters otherwise. Lossy
// but doesn't crash. Run on titles/descriptions before drawing.
String toAscii(const String& s);

// Word-wrap `s` into lines that each fit within `maxPxWidth` when
// rendered with `dev`'s current font/textSize. Newlines in `s` are
// honored as hard breaks. Lines containing only whitespace are kept
// to preserve paragraph spacing.
std::vector<String> wrap(LovyanGFX& dev, const String& s, int maxPxWidth);

// Shrink a string to fit `maxPxWidth`, appending an ellipsis when
// truncated. Single line.
String ellipsize(LovyanGFX& dev, const String& s, int maxPxWidth);

// A run of text on a single rendered line with consistent styling.
// Either plain article text (linkIndex == -1) or a piece of an
// internal link (linkIndex >= 0, indexing into the `links` vector
// produced by wrapTokens).
struct RenderedSegment {
    String text;
    int    linkIndex;   // -1 for plain text
};

// Link metadata produced as a side-output of token wrapping. The
// renderer uses these to drive Tab-cycle selection + enter-to-follow.
struct LinkInfo {
    String pageId;
    String text;       // canonical link text (first segment's text)
    int    firstLine;  // line index where this link first appears
};

// Word-wrap a token stream into screen lines. Each line is a list of
// RenderedSegments; segments within a line are contiguous in
// rendering and identical in styling. Spaces between tokens are
// retained inside the token text (the tokenizer doesn't strip them).
// `links` receives one entry per distinct Link token encountered;
// segments matching a link share its index.
std::vector<std::vector<RenderedSegment>> wrapTokens(
    LovyanGFX& dev,
    const std::vector<wiki::ExtractToken>& tokens,
    int maxPxWidth,
    std::vector<LinkInfo>& links);

} // namespace ui
