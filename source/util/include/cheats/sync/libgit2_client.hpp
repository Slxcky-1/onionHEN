#pragma once

#include "cheats/sync/i_git_client.hpp"
#include "cheats/sync/i_http_transport.hpp"

namespace onion::cheats::sync {

/** Adapter around libgit2. Optional HTTPS pipe is used on the console. */
class LibGit2Client final : public IGitClient {
public:
  explicit LibGit2Client(IHttpTransport *http = nullptr);
  ~LibGit2Client() override;

  GitStatus clone(const char *url, const char *dest,
                  const GitCloneOpts &opts) override;
  GitStatus fetch(const char *repo_dir) override;
  GitStatus headSha(const char *repo_dir, char *out, size_t out_size) override;
  GitStatus remoteUrl(const char *repo_dir, char *out,
                      size_t out_size) override;

private:
  IHttpTransport *http_;
  bool ready_ = false;
};

} // namespace onion::cheats::sync
