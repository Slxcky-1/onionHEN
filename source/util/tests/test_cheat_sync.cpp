#include "test_harness.h"

#include "cheats/sync/cheat_sync_engine.hpp"
#include "cheats/sync/i_cheat_catalog.hpp"
#include "cheats/sync/i_cheat_mirror.hpp"
#include "cheats/sync/i_http_transport.hpp"

#include <miniz.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using onion::cheats::sync::CheatMirrorId;
using onion::cheats::sync::CheatSyncEngine;
using onion::cheats::sync::HttpRequest;
using onion::cheats::sync::ICheatCatalog;
using onion::cheats::sync::ICheatMirror;
using onion::cheats::sync::IHttpTransport;
using onion::cheats::sync::SyncStatus;

namespace {

std::vector<std::string> g_flatten_roots;
int g_flatten_result = 0;

struct ProgressEvent {
  std::string phase;
  size_t completed = 0;
  size_t total = 0;
};

std::vector<ProgressEvent> g_progress_events;

const std::filesystem::path &test_root() {
  static const std::filesystem::path root =
      std::filesystem::path(ONION_DATA_ROOT) / "sync-engine";
  return root;
}

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
};

class FakeMirror final : public ICheatMirror {
public:
  FakeMirror(CheatMirrorId id, const char *name, const char *host,
             const char *url)
      : id_(id), name_(name), host_(host), url_(url) {}
  CheatMirrorId id() const override { return id_; }
  const char *name() const override { return name_; }
  const char *archiveHost() const override { return host_; }
  std::string archiveUrl(const ICheatCatalog &) const override { return url_; }

private:
  CheatMirrorId id_;
  const char *name_;
  const char *host_;
  const char *url_;
};

class MockHttp final : public IHttpTransport {
public:
  std::vector<SyncStatus> responses{SyncStatus::Ok};
  std::vector<unsigned char> archive;
  std::vector<std::string> urls;
  std::vector<std::string> hosts;
  size_t max_body_bytes = 0;

  SyncStatus perform(
      const HttpRequest &req,
      const std::function<SyncStatus(const void *, size_t)> &on_data) override {
    urls.emplace_back(req.url ? req.url : "");
    hosts.emplace_back(req.host_allow ? req.host_allow : "");
    max_body_bytes = req.max_body_bytes;
    const size_t index = urls.size() - 1;
    const SyncStatus response =
        index < responses.size() ? responses[index] : responses.back();
    if (response != SyncStatus::Ok) {
      return response;
    }
    if (req.on_progress) {
      req.on_progress(archive.size(), archive.size(), req.progress_user);
    }
    return on_data && !archive.empty()
               ? on_data(archive.data(), archive.size())
               : SyncStatus::Ok;
  }
};

std::vector<unsigned char> make_archive() {
  mz_zip_archive zip{};
  void *data = nullptr;
  size_t size = 0;
  const char cheat[] = "{\"name\":\"fixture\"}";
  const char ignored[] = "ignored";
  if (!mz_zip_writer_init_heap(&zip, 0, 0) ||
      !mz_zip_writer_add_mem(&zip, "repo-main/cheats/game.json", cheat,
                             sizeof(cheat) - 1, MZ_BEST_SPEED) ||
      !mz_zip_writer_add_mem(&zip, "repo-main/website/index.html", ignored,
                             sizeof(ignored) - 1, MZ_BEST_SPEED) ||
      !mz_zip_writer_finalize_heap_archive(&zip, &data, &size)) {
    mz_zip_writer_end(&zip);
    return {};
  }
  std::vector<unsigned char> out(static_cast<unsigned char *>(data),
                                 static_cast<unsigned char *>(data) + size);
  mz_free(data);
  mz_zip_writer_end(&zip);
  return out;
}

int capture_flatten(const char *root,
                    CheatSyncEngine::InstallProgressFn progress,
                    void *progress_user) {
  g_flatten_roots.emplace_back(root ? root : "");
  if (g_flatten_result != 0 || !root) {
    return g_flatten_result;
  }
  std::ifstream input(std::filesystem::path(root) / "game.json");
  std::string contents;
  std::getline(input, contents);
  if (contents != "{\"name\":\"fixture\"}") {
    return -1;
  }
  if (progress) {
    progress(0, 3, progress_user);
    progress(1, 3, progress_user);
    progress(2, 3, progress_user);
    progress(3, 3, progress_user);
  }
  return 0;
}

void capture_progress(const char *phase, size_t completed, size_t total,
                      void *) {
  g_progress_events.push_back(
      ProgressEvent{phase ? phase : "", completed, total});
}

void reset_test_root() {
  std::error_code error;
  std::filesystem::remove_all(test_root(), error);
  std::filesystem::create_directories(test_root(), error);
  g_flatten_roots.clear();
  g_flatten_result = 0;
  g_progress_events.clear();
}

bool temp_was_removed() {
  return !std::filesystem::exists(test_root() / "cheats_tmp");
}

} // namespace

static int test_download_extract_install_cleanup(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror mirror(CheatMirrorId::Github, "github", "codeload.github.com",
                    "https://codeload.github.com/org/fake/zip/refs/heads/main");
  MockHttp http;
  http.archive = make_archive();
  TEST_ASSERT_TRUE(!http.archive.empty());
  CheatSyncEngine engine(http, capture_flatten);
  engine.setProgressHandler(capture_progress, nullptr);
  const auto result = engine.run(catalog, mirror, nullptr, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Ok);
  TEST_ASSERT_TRUE(result.used_mirror == CheatMirrorId::Github);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(http.urls.size()));
  TEST_ASSERT_STREQ("codeload.github.com", http.hosts[0].c_str());
  TEST_ASSERT_TRUE(http.max_body_bytes == 64ull * 1024ull * 1024ull);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(g_flatten_roots.size()));
  TEST_ASSERT_TRUE(temp_was_removed());
  const auto install_begin = std::find_if(
      g_progress_events.begin(), g_progress_events.end(),
      [](const ProgressEvent &event) {
        return event.phase == "install" && event.completed == 0;
      });
  const auto install_end = std::find_if(
      g_progress_events.begin(), g_progress_events.end(),
      [](const ProgressEvent &event) {
        return event.phase == "install" && event.completed == 3 &&
               event.total == 3;
      });
  const auto cleanup = std::find_if(
      g_progress_events.begin(), g_progress_events.end(),
      [](const ProgressEvent &event) { return event.phase == "cleanup"; });
  TEST_ASSERT_TRUE(install_begin != g_progress_events.end());
  TEST_ASSERT_TRUE(install_end != g_progress_events.end());
  TEST_ASSERT_TRUE(cleanup != g_progress_events.end());
  TEST_ASSERT_TRUE(install_begin < install_end);
  TEST_ASSERT_TRUE(install_end < cleanup);
  return 0;
}

static int test_network_failure_uses_fallback(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror primary(CheatMirrorId::Cnb, "cnb", "cnb.cool",
                     "https://cnb.cool/org/fake/archive.zip");
  FakeMirror fallback(CheatMirrorId::Github, "github", "codeload.github.com",
                      "https://codeload.github.com/org/fake/archive.zip");
  MockHttp http;
  http.responses = {SyncStatus::Network, SyncStatus::Ok};
  http.archive = make_archive();
  CheatSyncEngine engine(http, capture_flatten);
  const auto result =
      engine.run(catalog, primary, &fallback, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Ok);
  TEST_ASSERT_TRUE(result.used_mirror == CheatMirrorId::Github);
  TEST_ASSERT_EQ_INT(2, static_cast<int>(http.urls.size()));
  TEST_ASSERT_STREQ("cnb.cool", http.hosts[0].c_str());
  TEST_ASSERT_STREQ("codeload.github.com", http.hosts[1].c_str());
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_explicit_source_failure_does_not_fallback(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror mirror(CheatMirrorId::Cnb, "cnb", "cnb.cool",
                    "https://cnb.cool/org/fake/archive.zip");
  MockHttp http;
  http.responses = {SyncStatus::Network};
  CheatSyncEngine engine(http, capture_flatten);
  const auto result = engine.run(catalog, mirror, nullptr, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Network);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(http.urls.size()));
  TEST_ASSERT_TRUE(g_flatten_roots.empty());
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_install_failure_does_not_switch_source(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror primary(CheatMirrorId::Github, "github", "codeload.github.com",
                     "https://codeload.github.com/org/fake/archive.zip");
  FakeMirror fallback(CheatMirrorId::Cnb, "cnb", "cnb.cool",
                      "https://cnb.cool/org/fake/archive.zip");
  MockHttp http;
  http.archive = make_archive();
  g_flatten_result = -1;
  CheatSyncEngine engine(http, capture_flatten);
  const auto result =
      engine.run(catalog, primary, &fallback, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Io);
  TEST_ASSERT_EQ_INT(1, static_cast<int>(http.urls.size()));
  TEST_ASSERT_TRUE(result.used_mirror == CheatMirrorId::Github);
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_non_https_archive_is_rejected(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror mirror(CheatMirrorId::Github, "github", "example.test",
                    "http://example.test/archive.zip");
  MockHttp http;
  CheatSyncEngine engine(http, capture_flatten);
  const auto result = engine.run(catalog, mirror, nullptr, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Rejected);
  TEST_ASSERT_TRUE(http.urls.empty());
  return 0;
}

static int test_tls_failure_uses_fallback(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror primary(CheatMirrorId::Cnb, "cnb", "cnb.cool",
                     "https://cnb.cool/org/fake/archive.zip");
  FakeMirror fallback(CheatMirrorId::Github, "github", "codeload.github.com",
                      "https://codeload.github.com/org/fake/archive.zip");
  MockHttp http;
  http.responses = {SyncStatus::Tls, SyncStatus::Ok};
  http.archive = make_archive();
  CheatSyncEngine engine(http, capture_flatten);
  const auto result =
      engine.run(catalog, primary, &fallback, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Ok);
  TEST_ASSERT_TRUE(result.used_mirror == CheatMirrorId::Github);
  TEST_ASSERT_EQ_INT(2, static_cast<int>(http.urls.size()));
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

static int test_clock_failure_sets_specific_error(void) {
  reset_test_root();
  FakeCatalog catalog;
  FakeMirror mirror(CheatMirrorId::Cnb, "cnb", "cnb.cool",
                    "https://cnb.cool/org/fake/archive.zip");
  MockHttp http;
  http.responses = {SyncStatus::Clock};
  CheatSyncEngine engine(http, capture_flatten);
  const auto result = engine.run(catalog, mirror, nullptr, test_root().c_str());
  TEST_ASSERT_TRUE(result.status == SyncStatus::Clock);
  TEST_ASSERT_STREQ("system_clock", result.error.c_str());
  TEST_ASSERT_EQ_INT(1, static_cast<int>(http.urls.size()));
  TEST_ASSERT_TRUE(temp_was_removed());
  return 0;
}

extern "C" int test_cheat_sync_suite(void) {
  int fails = 0;
  fails += onion_test_run("sync.archive_install",
                          test_download_extract_install_cleanup);
  fails += onion_test_run("sync.fallback", test_network_failure_uses_fallback);
  fails += onion_test_run("sync.no_fallback",
                          test_explicit_source_failure_does_not_fallback);
  fails += onion_test_run("sync.install_no_switch",
                          test_install_failure_does_not_switch_source);
  fails += onion_test_run("sync.reject_http", test_non_https_archive_is_rejected);
  fails += onion_test_run("sync.tls_fallback", test_tls_failure_uses_fallback);
  fails += onion_test_run("sync.clock_error", test_clock_failure_sets_specific_error);
  return fails;
}
