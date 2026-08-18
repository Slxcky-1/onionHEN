#pragma once

#include "cheats/sync/types.hpp"

#include <cstddef>
#include <functional>

namespace onion::cheats::sync {

struct HttpRequest {
  const char *url = nullptr;
  const char *method = "GET";
  const char *content_type = nullptr;
  const char *host_allow = nullptr;
  const void *body = nullptr;
  size_t body_len = 0;
  int timeout_ms = 0;
  int status_min = 200;
  int status_max = 299;
};

/**
 * Strategy: HTTPS byte pipe for git smart HTTP (and host tests).
 * No git and no catalog types.
 */
class IHttpTransport {
public:
  virtual ~IHttpTransport() = default;

  virtual GitStatus perform(
      const HttpRequest &req,
      const std::function<GitStatus(const void *, size_t)> &on_data) = 0;
};

} // namespace onion::cheats::sync
