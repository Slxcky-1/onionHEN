#include "test_harness.h"

#include "cheats/sync/http_probe.hpp"
#include "cheats/sync/i_http_transport.hpp"

#include <functional>
#include <string>

using onion::cheats::sync::GitStatus;
using onion::cheats::sync::HttpRequest;
using onion::cheats::sync::IHttpTransport;
using onion::cheats::sync::http_probe;

namespace {

class MockHttp final : public IHttpTransport {
public:
  GitStatus rc = GitStatus::Ok;
  std::string last_url;
  int last_timeout = -1;
  int last_min = -1;
  int last_max = -1;

  GitStatus perform(
      const HttpRequest &req,
      const std::function<GitStatus(const void *, size_t)> &) override {
    last_url = req.url ? req.url : "";
    last_timeout = req.timeout_ms;
    last_min = req.status_min;
    last_max = req.status_max;
    return rc;
  }
};

} // namespace

static int test_probe_accepts_redirect_range(void) {
  MockHttp http;
  TEST_ASSERT_TRUE(http_probe(http, "https://www.gstatic.com/generate_204",
                              "gstatic.com", 8000) == GitStatus::Ok);
  TEST_ASSERT_STREQ("https://www.gstatic.com/generate_204", http.last_url.c_str());
  TEST_ASSERT_EQ_INT(8000, http.last_timeout);
  TEST_ASSERT_EQ_INT(200, http.last_min);
  TEST_ASSERT_EQ_INT(399, http.last_max);
  return 0;
}

static int test_probe_rejects_empty_url(void) {
  MockHttp http;
  TEST_ASSERT_TRUE(http_probe(http, "", "x", 1000) == GitStatus::Rejected);
  TEST_ASSERT_TRUE(http.last_url.empty());
  return 0;
}

static int test_probe_forwards_network_error(void) {
  MockHttp http;
  http.rc = GitStatus::Network;
  TEST_ASSERT_TRUE(http_probe(http, "https://example.test/", "example.test",
                              1000) == GitStatus::Network);
  return 0;
}

extern "C" int test_http_probe_suite(void) {
  int fails = 0;
  fails += onion_test_run("http_probe.ok_range", test_probe_accepts_redirect_range);
  fails += onion_test_run("http_probe.empty_url", test_probe_rejects_empty_url);
  fails += onion_test_run("http_probe.network", test_probe_forwards_network_error);
  return fails;
}
