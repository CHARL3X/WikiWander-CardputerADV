#include "transport_device.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <cstdlib>

namespace wiki {

namespace {

constexpr int      kMaxRedirects   = 5;
constexpr uint32_t kReadTimeMs     = 20000;
constexpr uint32_t kTickIntervalMs = 50;

// Progress hook fired from inside the read-wait loops so the UI can
// animate the loading spinner while a fetch blocks. Throttled by
// kTickIntervalMs so the spinner draws at ~20 fps without thrashing
// the display during quick responses.
std::function<void()> g_netTick;
uint32_t g_lastTickMs = 0;

inline void readWait() {
    delay(1);
    if (g_netTick) {
        uint32_t now = millis();
        if (now - g_lastTickMs >= kTickIntervalMs) {
            g_netTick();
            g_lastTickMs = now;
        }
    }
}

struct ParsedUrl {
    bool        ok = false;
    bool        https = true;
    std::string host;
    uint16_t    port = 443;
    std::string path;  // path + query, leading "/"
};

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl out;
    size_t p = 0;
    if (url.rfind("https://", 0) == 0)      { out.https = true;  out.port = 443; p = 8; }
    else if (url.rfind("http://", 0) == 0)  { out.https = false; out.port = 80;  p = 7; }
    else                                    { return out; }

    auto slash = url.find('/', p);
    std::string hostport = (slash == std::string::npos)
        ? url.substr(p)
        : url.substr(p, slash - p);
    auto colon = hostport.find(':');
    if (colon == std::string::npos) {
        out.host = hostport;
    } else {
        out.host = hostport.substr(0, colon);
        out.port = static_cast<uint16_t>(std::atoi(hostport.c_str() + colon + 1));
    }
    out.path = (slash == std::string::npos) ? std::string("/") : url.substr(slash);
    out.ok = !out.host.empty() && !out.path.empty();
    return out;
}

std::string resolveRedirect(const std::string& base, const std::string& loc) {
    if (loc.empty()) return loc;
    if (loc.rfind("http://", 0) == 0 || loc.rfind("https://", 0) == 0) return loc;
    if (loc.rfind("//", 0) == 0) {
        auto colon = base.find(':');
        return (colon == std::string::npos ? std::string("https:") : base.substr(0, colon + 1)) + loc;
    }
    ParsedUrl b = parseUrl(base);
    if (!b.ok) return loc;
    std::string out = std::string(b.https ? "https://" : "http://") + b.host;
    if ((b.https && b.port != 443) || (!b.https && b.port != 80)) {
        out += ":" + std::to_string(b.port);
    }
    if (loc[0] != '/') out += "/";
    out += loc;
    return out;
}

bool ciStartsWith(const std::string& line, const char* prefix) {
    size_t plen = std::strlen(prefix);
    if (line.size() < plen) return false;
    for (size_t k = 0; k < plen; ++k) {
        char a = line[k]; if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
        char b = prefix[k]; if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';
        if (a != b) return false;
    }
    return true;
}

// Reads from the client until "\r\n", returns the line WITHOUT the
// terminator. Returns false on timeout or close.
bool readLine(WiFiClientSecure& c, std::string& out, uint32_t deadline) {
    out.clear();
    while (true) {
        if (millis() > deadline) return false;
        if (!c.available()) {
            if (!c.connected()) return false;
            readWait();
            continue;
        }
        char ch = c.read();
        out += ch;
        size_t n = out.size();
        if (n >= 2 && out[n - 2] == '\r' && out[n - 1] == '\n') {
            out.resize(n - 2);
            return true;
        }
    }
}

// ---------- ByteReader implementations over WiFiClientSecure ----------

// Content-Length mode: reads exactly N bytes then EOF.
class ContentLengthReader : public ByteReader {
public:
    ContentLengthReader(WiFiClientSecure& c, long remaining, uint32_t deadline)
        : c_(c), remaining_(remaining), deadline_(deadline) {}
    int read() override {
        if (remaining_ <= 0) return -1;
        while (!c_.available()) {
            if (!c_.connected()) return -1;
            if (millis() > deadline_) return -1;
            readWait();
        }
        --remaining_;
        return c_.read();
    }
    int available() const override {
        return remaining_ > 0 ? 1 : 0;
    }
private:
    WiFiClientSecure& c_;
    long remaining_;
    uint32_t deadline_;
};

// Chunked transfer-encoding: parses chunk headers inline.
class ChunkedReader : public ByteReader {
public:
    ChunkedReader(WiFiClientSecure& c, uint32_t deadline)
        : c_(c), chunkRemaining_(0), done_(false), deadline_(deadline) {}
    int read() override {
        if (done_) return -1;
        if (chunkRemaining_ == 0) {
            std::string lenLine;
            if (!readLine(c_, lenLine, deadline_)) { done_ = true; return -1; }
            long len = std::strtol(lenLine.c_str(), nullptr, 16);
            if (len <= 0) { done_ = true; return -1; }
            chunkRemaining_ = len;
        }
        while (!c_.available()) {
            if (!c_.connected()) { done_ = true; return -1; }
            if (millis() > deadline_) { done_ = true; return -1; }
            readWait();
        }
        int byte = c_.read();
        --chunkRemaining_;
        if (chunkRemaining_ == 0) {
            // Trailing CRLF after a chunk; ignore content.
            int crlf = 0;
            while (crlf < 2 && !done_) {
                if (millis() > deadline_) { done_ = true; break; }
                if (!c_.available()) {
                    if (!c_.connected()) { done_ = true; break; }
                    readWait(); continue;
                }
                c_.read();
                ++crlf;
            }
        }
        return byte;
    }
    int available() const override { return done_ ? 0 : 1; }
private:
    WiFiClientSecure& c_;
    long chunkRemaining_;
    bool done_;
    uint32_t deadline_;
};

// No content-length, not chunked: read until peer closes.
class UntilCloseReader : public ByteReader {
public:
    UntilCloseReader(WiFiClientSecure& c, uint32_t deadline)
        : c_(c), deadline_(deadline) {}
    int read() override {
        while (true) {
            if (millis() > deadline_) return -1;
            if (c_.available()) return c_.read();
            if (!c_.connected()) return -1;
            readWait();
        }
    }
    int available() const override { return 1; }
private:
    WiFiClientSecure& c_;
    uint32_t deadline_;
};

// ---------- one-hop fetch ----------

// Connects, sends GET, parses status + headers, then invokes
// `consumeBody` with the right ByteReader for the body's encoding.
// If 3xx is seen, returns immediately with r.error = "redirect:<URL>"
// so the outer loop can follow.
//
// Caller's consumeBody runs while the connection is open; after it
// returns, this function closes the socket.
HttpResult fetchOnce(const std::string& url,
                     const std::string& userAgent,
                     std::function<void(ByteReader&)> consumeBody) {
    HttpResult r{};
    r.status = 0;
    ParsedUrl u = parseUrl(url);
    if (!u.ok) { r.error = "bad url"; return r; }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(kReadTimeMs / 1000);

    if (!client.connect(u.host.c_str(), u.port)) {
        r.error = "connect failed: " + u.host;
        return r;
    }

    std::string req;
    req.reserve(256);
    req += "GET ";   req += u.path; req += " HTTP/1.1\r\n";
    req += "Host: "; req += u.host; req += "\r\n";
    req += "User-Agent: "; req += userAgent; req += "\r\n";
    req += "Accept: application/json,*/*;q=0.5\r\n";
    req += "Accept-Encoding: identity\r\n";
    req += "Connection: close\r\n\r\n";
    client.print(req.c_str());

    uint32_t deadline = millis() + kReadTimeMs;

    // Status line
    std::string statusLine;
    if (!readLine(client, statusLine, deadline)) {
        r.error = "no response";
        client.stop();
        return r;
    }
    if (statusLine.size() < 12 || statusLine.compare(0, 5, "HTTP/") != 0) {
        r.error = "bad status line";
        client.stop();
        return r;
    }
    r.status = std::atoi(statusLine.c_str() + 9);

    // Headers
    std::string location;
    bool chunked = false;
    long contentLength = -1;
    while (true) {
        std::string line;
        if (!readLine(client, line, deadline)) break;
        if (line.empty()) break;  // end of headers
        if (ciStartsWith(line, "location:")) {
            location = line.substr(9);
            while (!location.empty() && (location[0] == ' ' || location[0] == '\t'))
                location.erase(0, 1);
        } else if (ciStartsWith(line, "content-length:")) {
            contentLength = std::atol(line.c_str() + 15);
        } else if (ciStartsWith(line, "transfer-encoding:")) {
            if (line.find("chunked") != std::string::npos) chunked = true;
        }
    }

    if (r.status >= 300 && r.status < 400 && !location.empty()) {
        r.error = "redirect:" + location;
        client.stop();
        return r;
    }

    // Body
    if (chunked) {
        ChunkedReader reader(client, deadline);
        consumeBody(reader);
    } else if (contentLength > 0) {
        ContentLengthReader reader(client, contentLength, deadline);
        consumeBody(reader);
    } else {
        UntilCloseReader reader(client, deadline);
        consumeBody(reader);
    }

    client.stop();
    return r;
}

// Drains a ByteReader into a std::string. Used by buffered get().
void drainToString(ByteReader& reader, std::string& out) {
    char buf[256];
    while (true) {
        size_t n = reader.readBytes(buf, sizeof(buf));
        if (n == 0) break;
        out.append(buf, n);
    }
}

} // namespace

HttpResult DeviceTransport::get(const std::string& url,
                                const std::string& userAgent) {
    std::string current = url;
    for (int hop = 0; hop < kMaxRedirects; ++hop) {
        HttpResult r = fetchOnce(current, userAgent,
                                 [&r](ByteReader& reader) {
                                     drainToString(reader, r.body);
                                 });
        if (r.error.rfind("redirect:", 0) == 0) {
            std::string loc = r.error.substr(9);
            current = resolveRedirect(current, loc);
            continue;
        }
        return r;
    }
    HttpResult fail{};
    fail.status = 0;
    fail.error = "too many redirects";
    return fail;
}

HttpResult DeviceTransport::getStreamed(const std::string& url,
                                        const std::string& userAgent,
                                        std::function<void(ByteReader&)> consume) {
    std::string current = url;
    for (int hop = 0; hop < kMaxRedirects; ++hop) {
        HttpResult r = fetchOnce(current, userAgent, consume);
        if (r.error.rfind("redirect:", 0) == 0) {
            std::string loc = r.error.substr(9);
            current = resolveRedirect(current, loc);
            continue;
        }
        return r;
    }
    HttpResult fail{};
    fail.status = 0;
    fail.error = "too many redirects";
    return fail;
}

void setNetTick(std::function<void()> cb) {
    g_netTick = std::move(cb);
    g_lastTickMs = millis();
}

} // namespace wiki
