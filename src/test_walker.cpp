// Live end-to-end walker test. Hits real Wikipedia from the PC via
// CurlTransport + WikiClient, performs a 3-hop random walk, prints
// each step. Verifies the whole stack (transport, URL construction,
// parsing) against production data, not fixtures.
//
// Run from project root: test\run_walker.bat
//
// Network required. Not part of the unit-test loop.

#ifdef WIKIWANDER_PC_BUILD

#include "wiki_client.h"
#include "wiki_parser.h"
#include "wiki_types.h"
#include "extract_html.h"
#include "transport_curl.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

const char* kUserAgent =
    "CHARL3X-Wikiwander/0.1 (github.com/CHARL3X/CardputerLLM)";

void printSep() {
    std::printf("--------------------------------------------------\n");
}

// Print up to maxChars of `s`, escaping newlines so multi-paragraph
// extracts don't break the visual flow.
void printPreview(const std::string& s, size_t maxChars = 180) {
    std::string p = s.substr(0, maxChars);
    for (char& c : p) if (c == '\n') c = ' ';
    std::printf("%s%s\n", p.c_str(),
                s.size() > maxChars ? "..." : "");
}

void printSummary(const wiki::ArticleSummary& a) {
    std::printf("title:        %s\n",  a.title.c_str());
    std::printf("pageId:       %s\n",  a.pageId.c_str());
    std::printf("description:  %s\n",  a.description.c_str());
    std::printf("canonicalUrl: %s\n",  a.canonicalUrl.c_str());
    std::printf("extract (%zu chars):\n  ", a.extract.size());
    printPreview(a.extract, 200);
}

void printRelated(const std::vector<wiki::RelatedItem>& items) {
    for (size_t i = 0; i < items.size(); ++i) {
        std::printf("  %zu. %s\n", i + 1, items[i].title.c_str());
        std::printf("       %s\n", items[i].snippet.substr(0, 100).c_str());
    }
}

} // namespace

int main(int, char**) {
    std::printf("== Wikiwander live walker ==\n");
    std::printf("UA: %s\n\n", kUserAgent);

    wiki::CurlTransport transport;
    wiki::WikiClient client(&transport, kUserAgent);

    // ---- Hop 1: random ----
    printSep();
    std::printf("[1/3] random article\n");
    printSep();
    wiki::ArticleSummary article;
    if (!client.fetchRandom(article)) {
        std::printf("FAILED: %s\n", client.lastError().c_str());
        return 1;
    }
    printSummary(article);
    std::printf("\n");

    // Tokenize the extract_html of hop 1 to verify the parser
    // populates it and that the tokenizer finds inline links.
    {
        auto tokens = wiki::tokenizeExtractHtml(article.extractHtml);
        int linkCount = 0;
        for (auto& tk : tokens) if (tk.kind == wiki::ExtractToken::Link) ++linkCount;
        std::printf("extract_html: %zu chars  tokens=%zu  links=%d\n",
                    article.extractHtml.size(), tokens.size(), linkCount);
        // Print the first few links found so we can eyeball quality
        int shown = 0;
        for (auto& tk : tokens) {
            if (tk.kind != wiki::ExtractToken::Link) continue;
            std::printf("  link: '%s' -> %s\n", tk.text.c_str(), tk.pageId.c_str());
            if (++shown >= 5) break;
        }
        std::printf("\n");
    }

    // ---- Hop 2: morelike on that article ----
    printSep();
    std::printf("[2/3] morelike(%s)\n", article.pageId.c_str());
    printSep();
    std::vector<wiki::RelatedItem> related;
    if (!client.fetchMoreLike(article.pageId, related)) {
        std::printf("FAILED: %s\n", client.lastError().c_str());
        return 1;
    }
    if (related.empty()) {
        std::printf("(no related items returned)\n");
        // Not a hard failure -- some pages have no morelike hits.
        // Try again with a known-rich page for the walk.
        std::printf("Falling back to a known-rich page: 'Hypertext'\n");
        if (!client.fetchMoreLike("Hypertext", related)) {
            std::printf("FAILED fallback: %s\n", client.lastError().c_str());
            return 1;
        }
    }
    printRelated(related);
    std::printf("\n");

    // ---- Hop 3: open the first related article ----
    printSep();
    std::printf("[3/3] open '%s'\n", related[0].title.c_str());
    printSep();
    wiki::ArticleSummary next;
    if (!client.fetchByPageId(related[0].pageId, next)) {
        std::printf("FAILED: %s\n", client.lastError().c_str());
        return 1;
    }
    printSummary(next);
    std::printf("\n");

    // ---- Bonus: opensearch ----
    printSep();
    std::printf("[bonus] search('cardputer')\n");
    printSep();
    std::vector<wiki::SearchHit> hits;
    if (!client.search("cardputer", hits)) {
        std::printf("FAILED: %s\n", client.lastError().c_str());
        return 1;
    }
    for (size_t i = 0; i < hits.size() && i < 6; ++i) {
        std::printf("  %zu. %-30s  %s\n",
                    i + 1, hits[i].title.c_str(), hits[i].pageId.c_str());
    }

    // ---- Bonus: on this day (streamed parse against live API) ----
    printSep();
    std::printf("[bonus] onthisday(5, 27) -- streamed filter parse\n");
    printSep();
    std::vector<wiki::TodayEvent> events;
    if (!client.fetchOnThisDay(5, 27, events, 8)) {
        std::printf("FAILED: %s\n", client.lastError().c_str());
        return 1;
    }
    std::printf("got %zu events\n", events.size());
    for (size_t i = 0; i < events.size(); ++i) {
        std::printf("  %s%-6d  %s\n",
                    events[i].year < 0 ? "-" : " ",
                    events[i].year < 0 ? -events[i].year : events[i].year,
                    events[i].title.c_str());
    }

    // ---- Bonus: explicit fetch of a known-rich article so we can
    // verify extract_html tokenization on a non-stub. ----
    printSep();
    std::printf("[bonus] extract_html tokens for 'Hypertext'\n");
    printSep();
    wiki::ArticleSummary hyper;
    if (client.fetchByPageId("Hypertext", hyper)) {
        auto tokens = wiki::tokenizeExtractHtml(hyper.extractHtml);
        int textCount = 0, linkCount = 0;
        for (auto& tk : tokens) {
            if (tk.kind == wiki::ExtractToken::Link) ++linkCount;
            else ++textCount;
        }
        std::printf("extract_html=%zu chars  tokens=%zu (%d text, %d link)\n",
                    hyper.extractHtml.size(), tokens.size(), textCount, linkCount);
        int shown = 0;
        for (auto& tk : tokens) {
            if (tk.kind != wiki::ExtractToken::Link) continue;
            std::printf("  link: '%s' -> %s\n",
                        tk.text.c_str(), tk.pageId.c_str());
            if (++shown >= 8) break;
        }
    } else {
        std::printf("FAILED: %s\n", client.lastError().c_str());
    }

    std::printf("\n== walk complete ==\n");
    return 0;
}

#endif // WIKIWANDER_PC_BUILD
