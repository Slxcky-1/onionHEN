#include "cheats/sync/cheat_sync_engine.hpp"

#include <cstring>
#include <string>

namespace onion::cheats::sync {
namespace {

bool valid_catalog_id(const char *id) {
  if (!id || !id[0]) {
    return false;
  }
  for (const char *p = id; *p; ++p) {
    const unsigned char ch = static_cast<unsigned char>(*p);
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                    ch == '-' || ch == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool starts_with_https(const std::string &url) {
  return url.rfind("https://", 0) == 0;
}

std::string strip_dot_git(std::string url) {
  if (url.size() >= 4 && url.compare(url.size() - 4, 4, ".git") == 0) {
    url.resize(url.size() - 4);
  }
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }
  return url;
}

bool same_remote(const std::string &have, const std::string &want) {
  return strip_dot_git(have) == strip_dot_git(want);
}

std::string join_path(const char *a, const char *b) {
  std::string out = a ? a : "";
  if (!out.empty() && out.back() == '/') {
    out.pop_back();
  }
  out += '/';
  if (b && b[0] == '/') {
    ++b;
  }
  if (b) {
    out += b;
  }
  return out;
}

} // namespace

CheatSyncEngine::CheatSyncEngine(IGitClient &git, FlattenFn flatten,
                                 ExistsFn exists, RmtreeFn rmtree)
    : git_(git), flatten_(flatten), exists_(exists), rmtree_(rmtree) {}

GitStatus CheatSyncEngine::tryOne(const ICheatCatalog &catalog,
                                  const IGitMirror &mirror, const char *dest,
                                  Result &out) {
  const std::string url = mirror.cloneUrl(catalog);
  if (!starts_with_https(url)) {
    out.error = "refusing non-https remote";
    return GitStatus::Rejected;
  }
  out.url = url;
  out.used_mirror = mirror.id();

  const std::string git_dir = join_path(dest, ".git");
  char have_url[512] = {};
  const bool have_repo =
      exists_ && exists_(git_dir.c_str()) &&
      git_.remoteUrl(dest, have_url, sizeof(have_url)) == GitStatus::Ok &&
      same_remote(have_url, url);

  GitCloneOpts opts;
  opts.branch = catalog.defaultBranch();
  opts.depth = 1;
  opts.checkout_paths = catalog.flattenRoots(&opts.checkout_path_count);

  GitStatus st;
  if (have_repo) {
    st = git_.fetch(dest, opts);
  } else {
    if (exists_ && exists_(dest) && rmtree_) {
      (void)rmtree_(dest);
    }
    st = git_.clone(url.c_str(), dest, opts);
  }
  if (st != GitStatus::Ok) {
    out.error = have_repo ? "git fetch failed" : "git clone failed";
    return st;
  }

  char sha[64] = {};
  if (git_.headSha(dest, sha, sizeof(sha)) == GitStatus::Ok) {
    out.sha = sha;
  }
  return GitStatus::Ok;
}

CheatSyncEngine::Result CheatSyncEngine::run(const ICheatCatalog &catalog,
                                             const IGitMirror &primary,
                                             const IGitMirror *fallback,
                                             const char *data_root) {
  Result out;
  if (!flatten_ || !exists_ || !rmtree_ || !data_root || !data_root[0]) {
    out.error = "sync collaborators missing";
    return out;
  }
  if (!valid_catalog_id(catalog.id())) {
    out.error = "invalid catalog id";
    return out;
  }

  const std::string dest =
      join_path(join_path(data_root, "cheats_repo").c_str(), catalog.id());

  GitStatus st = tryOne(catalog, primary, dest.c_str(), out);
  if (is_network_failure(st) && fallback) {
    st = tryOne(catalog, *fallback, dest.c_str(), out);
  }
  if (st != GitStatus::Ok) {
    out.status = st;
    return out;
  }

  size_t n = 0;
  const char *const *roots = catalog.flattenRoots(&n);
  for (size_t i = 0; i < n; ++i) {
    const std::string root = join_path(dest.c_str(), roots[i]);
    if (flatten_(root.c_str()) != 0) {
      out.status = GitStatus::Io;
      out.error = "flatten failed";
      return out;
    }
    out.flattened_roots.push_back(root);
  }

  size_t junk_n = 0;
  const char *const *junk = catalog.discardAfterSync(&junk_n);
  for (size_t i = 0; i < junk_n; ++i) {
    const std::string path = join_path(dest.c_str(), junk[i]);
    if (exists_(path.c_str())) {
      (void)rmtree_(path.c_str());
    }
    out.discarded_paths.push_back(path);
  }

  out.status = GitStatus::Ok;
  out.error.clear();
  return out;
}

} // namespace onion::cheats::sync
