// Article store: save/list/load/delete .md files in /Wikiwander/articles/.
// File format matches CHARL3X's note files (YAML-ish frontmatter +
// body) so a future Memex-style app can scan both libraries with
// the same parser.
#pragma once
#include <Arduino.h>
#include "../../lib/wiki/wiki_types.h"
#include <vector>

namespace wiki_store {

struct SavedEntry {
    String slug;        // filename without .md
    String title;       // first-line title (from frontmatter)
    String savedUtc;    // ISO timestamp string
};

// Save an article. Returns true on success. Overwrites existing slug.
bool save(const wiki::ArticleSummary& a);

// Has this pageId been saved already?
bool exists(const String& pageId);

// List all saved articles, sorted newest first.
std::vector<SavedEntry> list();

// Load article body (extract text) for a saved slug. Returns empty
// string on miss.
String loadExtract(const String& slug);

// Load the full metadata + body. Title/description come from
// frontmatter; extract from the body.
bool load(const String& slug, wiki::ArticleSummary& out);

// Remove a saved article.
bool remove(const String& slug);

// Derive the SD filename slug from a Wikipedia pageId. Same shape as
// the on-disk file -- exposed so callers can preflight existence.
String slugFromPageId(const String& pageId);

} // namespace wiki_store
