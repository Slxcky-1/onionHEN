#pragma once

#include "cheats/sync/i_cheat_catalog.hpp"
#include "cheats/sync/types.hpp"

#include <memory>
#include <string>

namespace onion::cheats::sync {

/** Build https://{host}/{slug}.git. Mirror classes share this; no repo names. */
inline std::string https_clone_url(const char *host, const char *slug) {
  std::string url = "https://";
  if (host && host[0]) {
    url += host;
  }
  url += '/';
  if (slug && slug[0]) {
    url += slug;
  }
  url += ".git";
  return url;
}

/**
 * Strategy: one git *host*. Must not mention a specific cheat collection.
 */
class IGitMirror {
public:
  virtual ~IGitMirror() = default;

  virtual CheatMirrorId id() const = 0;
  virtual const char *name() const = 0;
  virtual const char *host() const = 0;
  /** Small HTTPS URL used for a generate_204-style reachability probe. */
  virtual const char *probeUrl() const = 0;
  /** Hostname allowlist for probeUrl() (may differ from git host()). */
  virtual const char *probeHost() const = 0;
  virtual std::string cloneUrl(const ICheatCatalog &catalog) const = 0;
};

std::unique_ptr<IGitMirror> make_github_mirror();
std::unique_ptr<IGitMirror> make_cnb_mirror();

} // namespace onion::cheats::sync
