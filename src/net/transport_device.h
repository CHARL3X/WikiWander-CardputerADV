// Device-side Transport implementation backed by WiFiClientSecure.
// Speaks HTTP/1.1, follows 301/302/303 redirects (required for
// Wikipedia's /random/summary which returns 303). Trusts any cert
// (setInsecure) for v1 -- root CA pinning is a stretch goal.
#pragma once
#include "../../lib/wiki/transport.h"

namespace wiki {

class DeviceTransport : public Transport {
public:
    DeviceTransport() = default;
    HttpResult get(const std::string& url,
                   const std::string& userAgent) override;
    HttpResult getStreamed(const std::string& url,
                           const std::string& userAgent,
                           std::function<void(ByteReader&)> consume) override;
};

// Install a callback fired from inside HTTP read-wait loops, throttled
// to ~50 ms. The UI uses this to animate the loading spinner while a
// blocking fetch is in flight (the network layer would otherwise hold
// the main thread for seconds with no chance to redraw). Pass nullptr
// to clear. Process-wide: only one Transport exists at a time.
void setNetTick(std::function<void()> cb);

} // namespace wiki
