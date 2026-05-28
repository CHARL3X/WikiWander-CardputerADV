#include "transport_curl.h"

#include <array>
#include <cstdio>
#include <sstream>
#include <string>

#ifdef _WIN32
  #define WIKIWANDER_POPEN  _popen
  #define WIKIWANDER_PCLOSE _pclose
#else
  #define WIKIWANDER_POPEN  popen
  #define WIKIWANDER_PCLOSE pclose
#endif

namespace wiki {

namespace {

// curl outputs the response body, then "\n<HTTP_STATUS_MARKER>:<code>".
// We sniff the last line for the marker so the rest of the buffer is
// the body. Marker is verbose enough that no real Wikipedia response
// would contain it accidentally.
constexpr const char* kStatusMarker = "___WIKIWANDER_HTTP_STATUS:";

// Shell-quote a string for cmd.exe (POSIX too -- " quoting is the
// common subset). Doubles any embedded `"` and wraps in quotes.
std::string quoteArg(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if (c == '"') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

} // namespace

CurlTransport::CurlTransport(std::string curlPath)
    : curlPath_(std::move(curlPath)) {}

HttpResult CurlTransport::get(const std::string& url,
                              const std::string& userAgent) {
    HttpResult r{};
    r.status = 0;

    // --silent: no progress bar
    // --location: follow redirects (random endpoint needs this)
    // --max-time 30: hard ceiling so a stuck request doesn't hang tests
    // -w: trailer with status code so we can split body from status
    std::ostringstream cmd;
    cmd << curlPath_
        << " --silent --location --max-time 30"
        << " -H " << quoteArg("User-Agent: " + userAgent)
        << " -w \"\\n" << kStatusMarker << "%{http_code}\""
        << " " << quoteArg(url)
        << " 2>&1";

    FILE* pipe = WIKIWANDER_POPEN(cmd.str().c_str(), "r");
    if (!pipe) {
        r.error = "popen failed";
        return r;
    }

    std::string raw;
    std::array<char, 4096> buf{};
    while (size_t n = std::fread(buf.data(), 1, buf.size(), pipe)) {
        raw.append(buf.data(), n);
    }
    int exitCode = WIKIWANDER_PCLOSE(pipe);
    if (exitCode != 0) {
        // curl writes its own error to stderr (folded into stdout via
        // 2>&1) -- pass it through as the error message.
        r.error = "curl exit " + std::to_string(exitCode);
        if (!raw.empty()) r.error += ": " + raw.substr(0, 200);
        return r;
    }

    // Split body off from the trailing status line. Find the LAST
    // occurrence of the marker -- the marker chars are vanishingly
    // unlikely to appear in a body, but lastIndexOf is the safe call.
    auto markerPos = raw.rfind(kStatusMarker);
    if (markerPos == std::string::npos) {
        r.error = "no status marker in curl output";
        return r;
    }
    std::string statusStr = raw.substr(markerPos + std::string(kStatusMarker).size());
    r.status = std::atoi(statusStr.c_str());

    // Body ends just before the marker's leading "\n" -- back up one
    // char if it's a newline so we don't keep curl's separator.
    size_t bodyEnd = markerPos;
    if (bodyEnd > 0 && raw[bodyEnd - 1] == '\n') --bodyEnd;
    r.body = raw.substr(0, bodyEnd);

    if (r.status == 0) {
        r.error = "transport returned status 0";
    }
    return r;
}

} // namespace wiki
