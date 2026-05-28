// Host-only Transport implementation that shells out to curl.exe.
// Lives under test/ because it's never built for the device -- the
// device has a WiFiClientSecure impl in src/net/ instead.
#pragma once

#include "transport.h"
#include <string>

namespace wiki {

class CurlTransport : public Transport {
public:
    // curlPath defaults to "curl" -- Windows 10+ ships it in
    // System32 and adds it to PATH for cmd/Powershell shells.
    explicit CurlTransport(std::string curlPath = "curl");
    HttpResult get(const std::string& url,
                   const std::string& userAgent) override;

private:
    std::string curlPath_;
};

} // namespace wiki
