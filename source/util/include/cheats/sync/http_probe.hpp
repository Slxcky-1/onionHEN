#pragma once

#include "cheats/sync/i_http_transport.hpp"

namespace onion::cheats::sync {

inline constexpr int kHttpProbeTimeoutMs = 8000;

/**
 * Android-style reachability check: GET @p url and accept 2xx/3xx.
 * Does not parse a cheat catalog. @p host_allow is the expected hostname.
 */
GitStatus http_probe(IHttpTransport &http, const char *url,
                     const char *host_allow, int timeout_ms);

} // namespace onion::cheats::sync
