#pragma once

#include "cheats/sync/i_http_transport.hpp"

namespace onion::cheats::sync {

/** SceHttp + SceSsl via runtime lookup. No link-time SceHttp stub required. */
class Ps5HttpTransport final : public IHttpTransport {
public:
  GitStatus perform(
      const HttpRequest &req,
      const std::function<GitStatus(const void *, size_t)> &on_data) override;
};

} // namespace onion::cheats::sync
