// HTTP GET interface used by WikiClient. Device build supplies a
// WiFiClientSecure-backed impl; the PC test harness supplies a curl-
// backed one. Keeping it abstract means the wiki client + parsers
// stay testable on the host without dragging Arduino headers in.
#pragma once

#include <cstring>
#include <functional>
#include <string>
#include <utility>

namespace wiki {

struct HttpResult {
    int         status;   // HTTP status code; 0 if transport-level failure
    std::string body;     // response body (assumed UTF-8)
    std::string error;    // human-readable error; empty on success
};

// Duck-typed read source for ArduinoJson. Any class providing read()
// returning -1 on EOF qualifies; the default readBytes loops over
// read() so subclasses only have to implement that one method.
//
// Used for streaming-parse paths where the response body is too big
// to buffer (e.g. Wikipedia's on-this-day feed at ~200KB+).
class ByteReader {
public:
    virtual ~ByteReader() = default;
    virtual int read() = 0;
    virtual size_t readBytes(char* buf, size_t len) {
        size_t got = 0;
        while (got < len) {
            int c = read();
            if (c < 0) break;
            buf[got++] = static_cast<char>(c);
        }
        return got;
    }
    // ArduinoJson optionally probes available() to pace its parser.
    // Returning a positive number keeps it eager.
    virtual int available() const { return 1; }
};

// In-memory ByteReader: wraps a std::string. Used by PC tests and as
// the base Transport's default streamed implementation.
class BufferReader : public ByteReader {
public:
    explicit BufferReader(std::string body)
        : body_(std::move(body)), pos_(0) {}
    int read() override {
        if (pos_ >= body_.size()) return -1;
        return static_cast<unsigned char>(body_[pos_++]);
    }
    size_t readBytes(char* buf, size_t len) override {
        size_t remaining = body_.size() - pos_;
        size_t n = (len < remaining) ? len : remaining;
        if (n > 0) {
            memcpy(buf, body_.data() + pos_, n);
            pos_ += n;
        }
        return n;
    }
    int available() const override {
        return static_cast<int>(body_.size() - pos_);
    }
private:
    std::string body_;
    size_t pos_;
};

class Transport {
public:
    virtual ~Transport() = default;

    // Single GET with a custom User-Agent header. Implementations
    // should follow redirects (status 30x) -- Wikipedia's random
    // endpoint relies on it.
    virtual HttpResult get(const std::string& url,
                           const std::string& userAgent) = 0;

    // Streamed GET: caller's `consume` receives the body as a
    // ByteReader. Use for responses too big to hold in RAM. Default
    // implementation buffers via get() then wraps in BufferReader --
    // device builds override to read straight off the socket.
    //
    // Returns the same HttpResult shape minus a populated body.
    virtual HttpResult getStreamed(const std::string& url,
                                   const std::string& userAgent,
                                   std::function<void(ByteReader&)> consume) {
        HttpResult r = get(url, userAgent);
        if (!r.error.empty() || r.status < 200 || r.status >= 300) return r;
        BufferReader br(std::move(r.body));
        consume(br);
        r.body.clear();
        return r;
    }
};

} // namespace wiki
