#include "cheats/sync/i_git_mirror.hpp"

#include <memory>

namespace onion::cheats::sync {
namespace {

class GithubMirror final : public IGitMirror {
public:
  CheatMirrorId id() const override { return CheatMirrorId::Github; }
  const char *name() const override { return "github"; }
  const char *host() const override { return "github.com"; }
  const char *probeUrl() const override {
    return "https://www.gstatic.com/generate_204";
  }
  const char *probeHost() const override { return "gstatic.com"; }
  std::string cloneUrl(const ICheatCatalog &catalog) const override {
    return https_clone_url(host(), catalog.slugFor(id()));
  }
};

} // namespace

std::unique_ptr<IGitMirror> make_github_mirror() {
  return std::unique_ptr<IGitMirror>(new GithubMirror());
}

} // namespace onion::cheats::sync
