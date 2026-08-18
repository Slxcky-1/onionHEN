#include "test_harness.h"

#include "cheats/sync/cheat_sync_engine.hpp"
#include "cheats/sync/i_cheat_catalog.hpp"
#include "cheats/sync/i_git_client.hpp"
#include "cheats/sync/i_git_mirror.hpp"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using onion::cheats::sync::CheatMirrorId;
using onion::cheats::sync::CheatSyncEngine;
using onion::cheats::sync::GitCloneOpts;
using onion::cheats::sync::GitStatus;
using onion::cheats::sync::ICheatCatalog;
using onion::cheats::sync::IGitClient;
using onion::cheats::sync::IGitMirror;
using onion::cheats::sync::https_clone_url;

namespace {

class FakeCatalog final : public ICheatCatalog {
public:
  const char *id() const override { return "fake-collection"; }
  const char *defaultBranch() const override { return "main"; }
  const char *slugFor(CheatMirrorId) const override { return "org/fake"; }
  const char *const *flattenRoots(size_t *count) const override {
    static const char *const kRoots[] = {"cheats"};
    if (count) {
      *count = 1;
    }
    return kRoots;
  }
  const char *const *discardAfterSync(size_t *count) const override {
    static const char *const kDiscard[] = {"website"};
    if (count) {
      *count = 1;
    }
    return kDiscard;
  }
};

class FakeMirror final : public IGitMirror {
public:
  FakeMirror(CheatMirrorId id, const char *name, const char *host)
      : id_(id), name_(name), host_(host) {}
  CheatMirrorId id() const override { return id_; }
  const char *name() const override { return name_; }
  const char *host() const override { return host_; }
  std::string cloneUrl(const ICheatCatalog &catalog) const override {
    return https_clone_url(host_, catalog.slugFor(id_));
  }

private:
  CheatMirrorId id_;
  const char *name_;
  const char *host_;
};

class MockGit final : public IGitClient {
public:
  GitStatus clone_rc = GitStatus::Ok;
  GitStatus fetch_rc = GitStatus::Ok;
  GitStatus next_clone_rc = GitStatus::Ok;
  int clones = 0;
  int fetches = 0;
  std::string last_url;
  std::string last_dest;
  std::string remote;
  std::string sha = "63926528deadbeef";

  GitStatus clone(const char *url, const char *dest,
                  const GitCloneOpts &) override {
    ++clones;
    last_url = url ? url : "";
    last_dest = dest ? dest : "";
    const GitStatus rc = next_clone_rc;
    next_clone_rc = clone_rc;
    if (rc == GitStatus::Ok) {
      remote = last_url;
    }
    return rc;
  }
  GitStatus fetch(const char *repo_dir) override {
    ++fetches;
    last_dest = repo_dir ? repo_dir : "";
    return fetch_rc;
  }
  GitStatus headSha(const char *, char *out, size_t out_size) override {
    if (!out || out_size == 0) {
      return GitStatus::Io;
    }
    std::snprintf(out, out_size, "%s", sha.c_str());
    return GitStatus::Ok;
  }
  GitStatus remoteUrl(const char *, char *out, size_t out_size) override {
    if (!out || out_size == 0) {
      return GitStatus::Io;
    }
    if (remote.empty()) {
      return GitStatus::Io;
    }
    std::snprintf(out, out_size, "%s", remote.c_str());
    return GitStatus::Ok;
  }
};

std::set<std::string> g_exists;
std::vector<std::string> g_rmtree;
std::vector<std::string> g_flatten;

bool mock_exists(const char *path) {
  return path && g_exists.count(path) != 0;
}
bool mock_rmtree(const char *path) {
  if (path) {
    g_rmtree.push_back(path);
    g_exists.erase(path);
  }
  return true;
}
int mock_flatten(const char *root) {
  if (root) {
    g_flatten.push_back(root);
  }
  return 0;
}

void reset_fs() {
  g_exists.clear();
  g_rmtree.clear();
  g_flatten.clear();
}

} // namespace

static int test_clone_then_flatten_from_catalog_root(void) {
  reset_fs();
  FakeCatalog cat;
  FakeMirror primary(CheatMirrorId::Github, "github", "github.com");
  MockGit git;
  CheatSyncEngine engine(git, mock_flatten, mock_exists, mock_rmtree);
  const auto r = engine.run(cat, primary, nullptr, "/tmp/onion-data");

  TEST_ASSERT_TRUE(r.status == GitStatus::Ok);
  TEST_ASSERT_EQ_INT(1, git.clones);
  TEST_ASSERT_EQ_INT(0, git.fetches);
  TEST_ASSERT_STREQ("https://github.com/org/fake.git", git.last_url.c_str());
  TEST_ASSERT_EQ_INT(1, static_cast<int>(r.flattened_roots.size()));
  TEST_ASSERT_STREQ("/tmp/onion-data/cheats_repo/fake-collection/cheats",
                    r.flattened_roots[0].c_str());
  TEST_ASSERT_EQ_INT(1, static_cast<int>(g_flatten.size()));
  TEST_ASSERT_STREQ(r.flattened_roots[0].c_str(), g_flatten[0].c_str());
  TEST_ASSERT_EQ_INT(1, static_cast<int>(r.discarded_paths.size()));
  TEST_ASSERT_STREQ("/tmp/onion-data/cheats_repo/fake-collection/website",
                    r.discarded_paths[0].c_str());
  TEST_ASSERT_STREQ("63926528deadbeef", r.sha.c_str());
  return 0;
}

static int test_fetch_when_same_remote_exists(void) {
  reset_fs();
  FakeCatalog cat;
  FakeMirror primary(CheatMirrorId::Cnb, "cnb", "cnb.cool");
  MockGit git;
  git.remote = "https://cnb.cool/org/fake.git";
  g_exists.insert("/tmp/onion-data/cheats_repo/fake-collection/.git");
  CheatSyncEngine engine(git, mock_flatten, mock_exists, mock_rmtree);
  const auto r = engine.run(cat, primary, nullptr, "/tmp/onion-data");
  TEST_ASSERT_TRUE(r.status == GitStatus::Ok);
  TEST_ASSERT_EQ_INT(0, git.clones);
  TEST_ASSERT_EQ_INT(1, git.fetches);
  return 0;
}

static int test_auto_fallback_on_network(void) {
  reset_fs();
  FakeCatalog cat;
  FakeMirror primary(CheatMirrorId::Cnb, "cnb", "cnb.cool");
  FakeMirror fallback(CheatMirrorId::Github, "github", "github.com");
  MockGit git;
  git.next_clone_rc = GitStatus::Network;
  git.clone_rc = GitStatus::Ok;
  CheatSyncEngine engine(git, mock_flatten, mock_exists, mock_rmtree);
  const auto r = engine.run(cat, primary, &fallback, "/tmp/onion-data");
  TEST_ASSERT_TRUE(r.status == GitStatus::Ok);
  TEST_ASSERT_EQ_INT(2, git.clones);
  TEST_ASSERT_TRUE(r.used_mirror == CheatMirrorId::Github);
  TEST_ASSERT_STREQ("https://github.com/org/fake.git", r.url.c_str());
  return 0;
}

static int test_explicit_no_fallback_on_network(void) {
  reset_fs();
  FakeCatalog cat;
  FakeMirror primary(CheatMirrorId::Github, "github", "github.com");
  MockGit git;
  git.clone_rc = GitStatus::Network;
  git.next_clone_rc = GitStatus::Network;
  CheatSyncEngine engine(git, mock_flatten, mock_exists, mock_rmtree);
  const auto r = engine.run(cat, primary, nullptr, "/tmp/onion-data");
  TEST_ASSERT_TRUE(r.status == GitStatus::Network);
  TEST_ASSERT_EQ_INT(1, git.clones);
  TEST_ASSERT_TRUE(g_flatten.empty());
  return 0;
}

static int test_flatten_failure_does_not_switch_mirror(void) {
  reset_fs();
  FakeCatalog cat;
  FakeMirror primary(CheatMirrorId::Github, "github", "github.com");
  FakeMirror fallback(CheatMirrorId::Cnb, "cnb", "cnb.cool");
  MockGit git;
  auto bad_flatten = [](const char *root) -> int {
    g_flatten.push_back(root ? root : "");
    return -1;
  };
  CheatSyncEngine engine(git, bad_flatten, mock_exists, mock_rmtree);
  const auto r = engine.run(cat, primary, &fallback, "/tmp/onion-data");
  TEST_ASSERT_TRUE(r.status == GitStatus::Io);
  TEST_ASSERT_EQ_INT(1, git.clones);
  TEST_ASSERT_TRUE(r.used_mirror == CheatMirrorId::Github);
  return 0;
}

extern "C" int test_cheat_sync_suite(void) {
  int fails = 0;
  fails += onion_test_run("sync.clone_flatten",
                          test_clone_then_flatten_from_catalog_root);
  fails += onion_test_run("sync.fetch_existing",
                          test_fetch_when_same_remote_exists);
  fails += onion_test_run("sync.fallback", test_auto_fallback_on_network);
  fails += onion_test_run("sync.no_fallback",
                          test_explicit_no_fallback_on_network);
  fails += onion_test_run("sync.flatten_no_switch",
                          test_flatten_failure_does_not_switch_mirror);
  return fails;
}
