#include "cheats/sync/http_transport_ps5.hpp"
#include <cstddef>

namespace onion::cheats::sync {
SyncStatus Ps5HttpTransport::perform(
    const HttpRequest &,
    const std::function<SyncStatus(const void *, std::size_t)> &) {
  return SyncStatus::Unavailable;
}
} // namespace onion::cheats::sync
