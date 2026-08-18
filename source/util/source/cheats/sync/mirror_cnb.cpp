#include "cheats/sync/i_git_mirror.hpp"

#include <memory>

namespace onion::cheats::sync {
namespace {

class CnbCoolMirror final : public IGitMirror {
public:
  CheatMirrorId id() const override { return CheatMirrorId::Cnb; }
  const char *name() const override { return "cnb"; }
  const char *host() const override { return "cnb.cool"; }
  std::string cloneUrl(const ICheatCatalog &catalog) const override {
    return https_clone_url(host(), catalog.slugFor(id()));
  }
};

} // namespace

std::unique_ptr<IGitMirror> make_cnb_mirror() {
  return std::unique_ptr<IGitMirror>(new CnbCoolMirror());
}

} // namespace onion::cheats::sync
