#include "text_utils.h"

namespace ui {

String toAscii(const String& s) {
    String out;
    out.reserve(s.length());
    int i = 0;
    while (i < (int)s.length()) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x80) {
            out += (char)c;
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8: try to recover common Latin-1 codepoints
            // we have ASCII analogues for, otherwise '?'.
            if (i + 1 < (int)s.length()) {
                uint32_t cp = ((c & 0x1F) << 6) | ((uint8_t)s[i+1] & 0x3F);
                char a = '?';
                switch (cp) {
                    case 0x00A0: a = ' '; break;       // nbsp
                    case 0x00B7: a = '.'; break;       // middot
                    case 0x00BF: a = '?'; break;
                    case 0x2013: case 0x2014: a = '-'; break; // en/em dash
                    case 0x2018: case 0x2019: a = '\''; break;
                    case 0x201C: case 0x201D: a = '"'; break;
                    case 0x2026: a = '.'; break;       // ellipsis -> '.'
                    case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3:
                    case 0x00C4: case 0x00C5: a = 'A'; break;
                    case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3:
                    case 0x00E4: case 0x00E5: a = 'a'; break;
                    case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: a = 'E'; break;
                    case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: a = 'e'; break;
                    case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: a = 'I'; break;
                    case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: a = 'i'; break;
                    case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5:
                    case 0x00D6: a = 'O'; break;
                    case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5:
                    case 0x00F6: a = 'o'; break;
                    case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC: a = 'U'; break;
                    case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: a = 'u'; break;
                    case 0x00D1: a = 'N'; break;
                    case 0x00F1: a = 'n'; break;
                    case 0x00C7: a = 'C'; break;
                    case 0x00E7: a = 'c'; break;
                    default: a = '?'; break;
                }
                out += a;
                i += 2;
            } else { out += '?'; ++i; }
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8: handle a few common punctuation codepoints
            if (i + 2 < (int)s.length()) {
                uint32_t cp = ((c & 0x0F) << 12) |
                              (((uint8_t)s[i+1] & 0x3F) << 6) |
                               ((uint8_t)s[i+2] & 0x3F);
                char a = '?';
                if (cp == 0x2013 || cp == 0x2014) a = '-';
                else if (cp == 0x2018 || cp == 0x2019) a = '\'';
                else if (cp == 0x201C || cp == 0x201D) a = '"';
                else if (cp == 0x2026) a = '.';
                out += a;
                i += 3;
            } else { out += '?'; ++i; }
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte UTF-8: drop entirely
            out += '?';
            i += (i + 4 <= (int)s.length() ? 4 : 1);
        } else {
            out += '?';
            ++i;
        }
    }
    return out;
}

std::vector<String> wrap(LovyanGFX& dev, const String& s, int maxPxWidth) {
    std::vector<String> out;
    String line;
    String word;
    auto flushLine = [&]() {
        out.push_back(line);
        line = "";
    };
    auto tryFitWord = [&](const String& w) {
        if (line.length() == 0) {
            line = w;
        } else {
            String probe = line + " " + w;
            if (dev.textWidth(probe.c_str()) <= maxPxWidth) line = probe;
            else { flushLine(); line = w; }
        }
        // If single word still exceeds, hard-cut
        while (dev.textWidth(line.c_str()) > maxPxWidth && line.length() > 1) {
            // Greedy: pop chars off until it fits, flush, restart with the rest
            String fit = line;
            while (fit.length() > 1 && dev.textWidth(fit.c_str()) > maxPxWidth) {
                fit.remove(fit.length() - 1);
            }
            out.push_back(fit);
            line = line.substring(fit.length());
        }
    };
    for (int i = 0; i <= (int)s.length(); ++i) {
        char c = (i < (int)s.length()) ? s[i] : ' ';
        if (c == '\n') {
            if (word.length() > 0) { tryFitWord(word); word = ""; }
            flushLine();
        } else if (c == ' ' || i == (int)s.length()) {
            if (word.length() > 0) { tryFitWord(word); word = ""; }
        } else {
            word += c;
        }
    }
    if (line.length() > 0) flushLine();
    return out;
}

String ellipsize(LovyanGFX& dev, const String& s, int maxPxWidth) {
    if (dev.textWidth(s.c_str()) <= maxPxWidth) return s;
    String t = s;
    const char* dots = "...";
    int dotsW = dev.textWidth(dots);
    while (t.length() > 0 && dev.textWidth(t.c_str()) + dotsW > maxPxWidth) {
        t.remove(t.length() - 1);
    }
    return t + dots;
}

namespace {

// Extract the next whitespace-delimited word starting at `pos` in
// `s`. Leading spaces are absorbed into the returned word so the
// renderer can keep "natural" gaps between words. Returns empty when
// no more words remain.
String takeNextWord(const String& s, int& pos) {
    int n = (int)s.length();
    if (pos >= n) return String();
    int start = pos;
    // Eat leading whitespace as part of this word
    while (pos < n && s[pos] == ' ') ++pos;
    // Then the actual word characters (anything not space)
    while (pos < n && s[pos] != ' ') ++pos;
    return s.substring(start, pos);
}

void emitSegment(std::vector<RenderedSegment>& line,
                 const String& text,
                 int linkIndex) {
    // Merge with the last segment if it shares the same styling --
    // keeps the segment count low so render iteration is cheap.
    if (!line.empty() && line.back().linkIndex == linkIndex) {
        line.back().text += text;
    } else {
        RenderedSegment s;
        s.text = text;
        s.linkIndex = linkIndex;
        line.push_back(std::move(s));
    }
}

int lineWidthFor(LovyanGFX& dev,
                 const std::vector<RenderedSegment>& line) {
    int w = 0;
    for (auto& seg : line) w += dev.textWidth(seg.text.c_str());
    return w;
}

} // namespace

std::vector<std::vector<RenderedSegment>> wrapTokens(
    LovyanGFX& dev,
    const std::vector<wiki::ExtractToken>& tokens,
    int maxPxWidth,
    std::vector<LinkInfo>& links) {

    std::vector<std::vector<RenderedSegment>> lines;
    std::vector<RenderedSegment> current;
    links.clear();

    for (auto& tok : tokens) {
        bool isLink = (tok.kind == wiki::ExtractToken::Link);
        int linkIndex = -1;
        if (isLink) {
            // Register a new LinkInfo; firstLine will be back-filled
            // once we know which rendered line carries its first
            // segment.
            LinkInfo info;
            info.pageId = String(tok.pageId.c_str());
            info.text   = String(tok.text.c_str());
            info.firstLine = -1;
            linkIndex = (int)links.size();
            links.push_back(std::move(info));
        }

        // Sanitize the token text down to ASCII so the bitmap font
        // doesn't render boxes for non-Latin characters.
        String src = toAscii(String(tok.text.c_str()));
        int pos = 0;
        while (pos < (int)src.length()) {
            String word = takeNextWord(src, pos);
            if (word.length() == 0) break;

            // Compute width if we appended this word to the current line
            // (with current line's existing width plus this word's).
            int curW = lineWidthFor(dev, current);
            int wordW = dev.textWidth(word.c_str());

            if (curW + wordW > maxPxWidth && !current.empty()) {
                // Flush current line; start new line with this word
                lines.push_back(std::move(current));
                current.clear();
                // Trim any leading space from `word` -- start of line
                while (word.length() > 0 && word[0] == ' ') {
                    word.remove(0, 1);
                }
                if (word.length() == 0) continue;
            }

            // Back-fill firstLine for this link if not yet set
            if (isLink && links[linkIndex].firstLine < 0) {
                links[linkIndex].firstLine = (int)lines.size();
            }

            emitSegment(current, word, linkIndex);
        }
    }
    if (!current.empty()) lines.push_back(std::move(current));
    return lines;
}

} // namespace ui
