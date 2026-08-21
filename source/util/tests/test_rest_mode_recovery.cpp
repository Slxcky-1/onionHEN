/* Host tests for the rest-mode recovery state machine (rest_mode.hpp).

 * SIGCONT resume + failed-inject retry. No PS5 SDK, no signals.
 */
#include "test_harness.h"
#include "rest_mode.hpp"

namespace {

using onion::rest_mode::Action;
using onion::rest_mode::Recovery;
using onion::rest_mode::ToolboxReinjector;

class FakeReinjector final : public ToolboxReinjector {
 public:
  int calls = 0;
  bool last_rest_resume = false;
  bool succeed = true;
  bool reinject(bool rest_resume) override {
    ++calls;
    last_rest_resume = rest_resume;
    return succeed;
  }
};

static int test_resume_injects(void) {
  FakeReinjector reinjector;
  Recovery r(reinjector);

  r.on_resume();
  TEST_ASSERT_TRUE(r.poll() == Action::Reinject);
  TEST_ASSERT_EQ_INT(1, reinjector.calls);
  TEST_ASSERT_TRUE(reinjector.last_rest_resume);
  TEST_ASSERT_TRUE(r.poll() == Action::None);
  return 0;
}

static int test_failed_reinject_retries_until_success(void) {
  FakeReinjector reinjector;
  Recovery r(reinjector);

  reinjector.succeed = false;
  r.on_resume();
  TEST_ASSERT_TRUE(r.poll() == Action::None);
  TEST_ASSERT_EQ_INT(1, reinjector.calls);
  TEST_ASSERT_TRUE(r.is_armed());

  TEST_ASSERT_TRUE(r.poll() == Action::None);
  TEST_ASSERT_EQ_INT(2, reinjector.calls);

  reinjector.succeed = true;
  TEST_ASSERT_TRUE(r.poll() == Action::Reinject);
  TEST_ASSERT_EQ_INT(3, reinjector.calls);
  TEST_ASSERT_TRUE(!r.is_armed());
  TEST_ASSERT_TRUE(r.poll() == Action::None);
  return 0;
}

static int test_poll_without_resume_does_nothing(void) {
  FakeReinjector reinjector;
  Recovery r(reinjector);

  TEST_ASSERT_TRUE(r.poll() == Action::None);
  TEST_ASSERT_EQ_INT(0, reinjector.calls);
  return 0;
}

static int test_reset_clears_armed_state(void) {
  FakeReinjector reinjector;
  Recovery r(reinjector);

  r.on_resume();
  TEST_ASSERT_TRUE(r.is_armed());
  r.reset();
  TEST_ASSERT_TRUE(!r.is_armed());
  TEST_ASSERT_TRUE(r.poll() == Action::None);
  TEST_ASSERT_EQ_INT(0, reinjector.calls);
  return 0;
}

}  // namespace

extern "C" int test_rest_mode_recovery_suite(void) {
  int failures = 0;
  failures += onion_test_run("rest_mode.resume_injects", test_resume_injects);
  failures += onion_test_run("rest_mode.failed_reinject_retries",
                             test_failed_reinject_retries_until_success);
  failures += onion_test_run("rest_mode.poll_without_resume",
                             test_poll_without_resume_does_nothing);
  failures += onion_test_run("rest_mode.reset_clears",
                             test_reset_clears_armed_state);
  return failures;
}
