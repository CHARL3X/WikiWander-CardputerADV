#include "wiki_store.h"
#include <SD.h>
#include <time.h>
#include <algorithm>

namespace {

constexpr const char* kDir = "/Wikiwander/articles";

String sanitizeSlug(const String& s) {
    // FAT-safe: ASCII letters/digits/underscore only. Anything else
    // becomes underscore. Cap at 48 chars so the full path stays
    // well under the 255-char FAT limit even with .md suffix.
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length() && out.length() < 48; ++i) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-';
        out += ok ? c : '_';
    }
    if (out.length() == 0) out = "article";
    return out;
}

String pathFor(const String& slug) {
    return String(kDir) + "/" + slug + ".md";
}

String isoUtcNow() {
    time_t now = time(nullptr);
    if (now < 1577836800) return String("1970-01-01T00:00:00Z");
    struct tm t;
    gmtime_r(&now, &t);
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
}

// Escape YAML-ish frontmatter values: replace newlines with spaces,
// strip leading/trailing whitespace. We don't quote because Wikipedia
// titles can contain quotes; instead we keep them on a single line
// and trust them.
String frontmatterValue(const String& s) {
    String v = s;
    v.replace('\n', ' ');
    v.replace('\r', ' ');
    v.trim();
    return v;
}

String readWhole(const String& path) {
    if (!SD.exists(path)) return String();
    File f = SD.open(path, FILE_READ);
    if (!f) return String();
    String out;
    while (f.available()) out += (char)f.read();
    f.close();
    return out;
}

String extractFrontmatterField(const String& md, const char* key) {
    // Naive line-by-line: stop at the closing "---" or EOF. Looking
    // for "<key>: <value>" with optional leading whitespace.
    int start = md.indexOf("---");
    if (start != 0) return String();
    int end = md.indexOf("\n---", 3);
    if (end < 0) return String();
    String head = md.substring(3, end);
    String prefix = String(key) + ":";
    int i = 0;
    while (i < (int)head.length()) {
        int nl = head.indexOf('\n', i);
        if (nl < 0) nl = head.length();
        String line = head.substring(i, nl);
        line.trim();
        if (line.startsWith(prefix)) {
            String v = line.substring(prefix.length());
            v.trim();
            return v;
        }
        i = nl + 1;
    }
    return String();
}

String extractBody(const String& md) {
    int end = md.indexOf("\n---", 3);
    if (end < 0) return md;
    int bodyStart = end + 4;
    while (bodyStart < (int)md.length() &&
           (md[bodyStart] == '\n' || md[bodyStart] == '\r'))
        ++bodyStart;
    return md.substring(bodyStart);
}

} // namespace

namespace wiki_store {

String slugFromPageId(const String& pageId) {
    return sanitizeSlug(pageId);
}

bool save(const wiki::ArticleSummary& a) {
    String slug = sanitizeSlug(String(a.pageId.c_str()));
    String path = pathFor(slug);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;

    f.print("---\n");
    f.print("title: ");        f.print(frontmatterValue(String(a.title.c_str()))); f.print("\n");
    f.print("page_id: ");      f.print(frontmatterValue(String(a.pageId.c_str()))); f.print("\n");
    f.print("description: ");  f.print(frontmatterValue(String(a.description.c_str()))); f.print("\n");
    f.print("source_url: ");   f.print(frontmatterValue(String(a.canonicalUrl.c_str()))); f.print("\n");
    f.print("saved_utc: ");    f.print(isoUtcNow()); f.print("\n");
    f.print("---\n\n");
    f.print(a.extract.c_str());
    f.print("\n");
    f.close();
    return true;
}

bool exists(const String& pageId) {
    return SD.exists(pathFor(sanitizeSlug(pageId)));
}

std::vector<SavedEntry> list() {
    std::vector<SavedEntry> out;
    File dir = SD.open(kDir);
    if (!dir || !dir.isDirectory()) return out;

    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) { f.close(); continue; }
        String name = f.name();
        // SD library returns names sometimes as full path, sometimes
        // bare -- handle both.
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (!name.endsWith(".md")) { f.close(); continue; }

        SavedEntry e;
        e.slug = name.substring(0, name.length() - 3);
        String full;
        while (f.available()) full += (char)f.read();
        f.close();
        e.title    = extractFrontmatterField(full, "title");
        e.savedUtc = extractFrontmatterField(full, "saved_utc");
        if (e.title.length() == 0) e.title = e.slug;
        out.push_back(std::move(e));
    }
    dir.close();

    // Newest first by saved_utc string (ISO sorts lexically).
    std::sort(out.begin(), out.end(),
              [](const SavedEntry& a, const SavedEntry& b) {
                  return a.savedUtc > b.savedUtc;
              });
    return out;
}

String loadExtract(const String& slug) {
    return extractBody(readWhole(pathFor(slug)));
}

bool load(const String& slug, wiki::ArticleSummary& out) {
    String md = readWhole(pathFor(slug));
    if (md.length() == 0) return false;
    String t  = extractFrontmatterField(md, "title");
    String p  = extractFrontmatterField(md, "page_id");
    String d  = extractFrontmatterField(md, "description");
    String u  = extractFrontmatterField(md, "source_url");
    String b  = extractBody(md);
    out.title.assign(t.c_str());
    out.pageId.assign(p.c_str());
    out.description.assign(d.c_str());
    out.canonicalUrl.assign(u.c_str());
    out.extract.assign(b.c_str());
    return out.title.size() > 0 || out.pageId.size() > 0;
}

bool remove(const String& slug) {
    return SD.remove(pathFor(slug));
}

} // namespace wiki_store
