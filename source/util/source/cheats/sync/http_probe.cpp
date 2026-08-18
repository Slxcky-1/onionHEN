#include "cheats/sync/http_probe.hpp"

namespace onion::cheats::sync {

GitStatus http_probe(IHttpTransport &http, const char *url,
                     const char *host_allow, int timeout_ms) {
  if (!url || !url[0]) {
    return GitStatus::Rejected;
  }
  HttpRequest req;
  req.url = url;
  req.method = "GET";
  req.host_allow = host_allow;
  req.timeout_ms = timeout_ms;
  req.status_min = 200;
  req.status_max = 399;
  return http.perform(req, nullptr);
}

} // namespace onion::cheats::sync
