#include "cheats/sync/libgit2_client.hpp"

#include <onion/log.h>

#include <cstdio>
#include <cstring>

#if defined(ONION_HAVE_LIBGIT2)
#include <git2.h>
#endif

namespace onion::cheats::sync {
namespace {

#if defined(ONION_HAVE_LIBGIT2)
GitStatus map_git_error(int rc) {
  if (rc == 0) {
    return GitStatus::Ok;
  }
  const git_error *err = git_error_last();
  const char *msg = err && err->message ? err->message : "";
  LOG_ERROR("libgit2: %s (%d)", msg, rc);
  if (err && (err->klass == GIT_ERROR_NET || err->klass == GIT_ERROR_SSL ||
              err->klass == GIT_ERROR_HTTP)) {
    return GitStatus::Network;
  }
  if (err && err->klass == GIT_ERROR_OS) {
    return GitStatus::Io;
  }
  return GitStatus::Protocol;
}

void copy_bounded(char *out, size_t out_size, const char *src) {
  if (!out || out_size == 0) {
    return;
  }
  if (!src) {
    out[0] = '\0';
    return;
  }
  std::snprintf(out, out_size, "%s", src);
}
#endif

} // namespace

LibGit2Client::LibGit2Client(IHttpTransport *http) : http_(http) {
#if defined(ONION_HAVE_LIBGIT2)
  if (git_libgit2_init() >= 0) {
    ready_ = true;
  } else {
    LOG_ERROR("git_libgit2_init failed");
  }
#else
  (void)http_;
  ready_ = false;
#endif
}

LibGit2Client::~LibGit2Client() {
#if defined(ONION_HAVE_LIBGIT2)
  if (ready_) {
    git_libgit2_shutdown();
  }
#endif
}

GitStatus LibGit2Client::clone(const char *url, const char *dest,
                               const GitCloneOpts &opts) {
#if defined(ONION_HAVE_LIBGIT2)
  if (!ready_ || !url || !dest) {
    return GitStatus::Unavailable;
  }
  git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
  if (opts.branch && opts.branch[0]) {
    clone_opts.checkout_branch = opts.branch;
  }
  if (opts.depth > 0) {
    clone_opts.fetch_opts.depth = opts.depth;
  }
  git_repository *repo = nullptr;
  const int rc = git_clone(&repo, url, dest, &clone_opts);
  if (repo) {
    git_repository_free(repo);
  }
  return map_git_error(rc);
#else
  (void)url;
  (void)dest;
  (void)opts;
  LOG_ERROR("libgit2 was not linked into this build");
  return GitStatus::Unavailable;
#endif
}

GitStatus LibGit2Client::fetch(const char *repo_dir) {
#if defined(ONION_HAVE_LIBGIT2)
  if (!ready_ || !repo_dir) {
    return GitStatus::Unavailable;
  }
  git_repository *repo = nullptr;
  int rc = git_repository_open(&repo, repo_dir);
  if (rc != 0) {
    return map_git_error(rc);
  }
  git_remote *remote = nullptr;
  rc = git_remote_lookup(&remote, repo, "origin");
  if (rc != 0) {
    git_repository_free(repo);
    return map_git_error(rc);
  }
  git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
  fetch_opts.depth = 1;
  rc = git_remote_fetch(remote, nullptr, &fetch_opts, nullptr);
  if (rc == 0) {
    git_object *target = nullptr;
    if (git_revparse_single(&target, repo, "origin/HEAD") == 0 ||
        git_revparse_single(&target, repo, "FETCH_HEAD") == 0) {
      git_checkout_options co = GIT_CHECKOUT_OPTIONS_INIT;
      co.checkout_strategy = GIT_CHECKOUT_FORCE;
      (void)git_checkout_tree(repo, target, &co);
      git_object_free(target);
    }
  }
  git_remote_free(remote);
  git_repository_free(repo);
  return map_git_error(rc);
#else
  (void)repo_dir;
  return GitStatus::Unavailable;
#endif
}

GitStatus LibGit2Client::headSha(const char *repo_dir, char *out,
                                 size_t out_size) {
#if defined(ONION_HAVE_LIBGIT2)
  if (!ready_ || !repo_dir || !out || out_size < 8) {
    return GitStatus::Unavailable;
  }
  git_repository *repo = nullptr;
  int rc = git_repository_open(&repo, repo_dir);
  if (rc != 0) {
    return map_git_error(rc);
  }
  git_oid oid;
  rc = git_reference_name_to_id(&oid, repo, "HEAD");
  if (rc == 0) {
    char buf[GIT_OID_HEXSZ + 1] = {};
    git_oid_tostr(buf, sizeof(buf), &oid);
    copy_bounded(out, out_size, buf);
  }
  git_repository_free(repo);
  return map_git_error(rc);
#else
  (void)repo_dir;
  (void)out;
  (void)out_size;
  return GitStatus::Unavailable;
#endif
}

GitStatus LibGit2Client::remoteUrl(const char *repo_dir, char *out,
                                   size_t out_size) {
#if defined(ONION_HAVE_LIBGIT2)
  if (!ready_ || !repo_dir || !out || out_size == 0) {
    return GitStatus::Unavailable;
  }
  git_repository *repo = nullptr;
  int rc = git_repository_open(&repo, repo_dir);
  if (rc != 0) {
    return map_git_error(rc);
  }
  git_remote *remote = nullptr;
  rc = git_remote_lookup(&remote, repo, "origin");
  if (rc == 0) {
    copy_bounded(out, out_size, git_remote_url(remote));
    git_remote_free(remote);
  }
  git_repository_free(repo);
  return map_git_error(rc);
#else
  (void)repo_dir;
  (void)out;
  (void)out_size;
  return GitStatus::Unavailable;
#endif
}

} // namespace onion::cheats::sync
