/* Host tests for the rest-mode recovery state machine (rest_mode.hpp).

 * Exercises the pure decision logic via fake probe/reinjector collaborators —
 * no PS5 SDK, no signals, no real network.
 */
#include "test_harness.h"
#include "rest_mode.hpp"

#include <cstdio>

namespace {

using onion::rest_mode::Action;
using onion::rest_mode::NetworkProbe;
using onion::rest_mode::Recovery;
using onion::rest_mode::ToolboxReinjector;

class FakeProbe final : public NetworkProbe {
 public:
  bool up = true;
  bool is_up() const override { return up; }
};

class FakeReinjector final : public ToolboxReinjector {
 public:
  int calls = 0;
  bool last_rest_resume = false;
  void reinject(bool rest_resume) override {
    ++calls;
    last_rest_resume = rest_resume;
  }
};

static int test_resume_waits_for_network(void) {
  FakeProbe probe;
  FakeReinjector reinjector;
  Recovery r(probe, reinjector);

  probe.up = false;
  r.on_resume();
  TEST_ASSERT_TRUE(r.poll() == Action::None);
  TEST_ASSERT_EQ_INT(0, reinjector.calls);

  probe.up = true;
  TEST_ASSERT_TRUE(r.poll() == Action::Reinject);
  TEST_ASSERT_EQ_INT(1, reinjector.calls);
  TEST_ASSERT_TRUE(reinjector.last_rest_resume);

  /* One-shot: disarmed after reinject. */
  TEST_ASSERT_TRUE(r.poll() == Action::None);
  return 0;
}

static int test_standby_then_network_drop_fallback(void) {
  FakeProbe probe;
  FakeReinjector reinjector;
  Recovery r(probe, reinjector);

  probe.up = true;
  r.on_standby();
  TEST_ASSERT_TRUE(r.poll() == Action::None); /* up, no resume, no drop */

  probe.up = false;
  TEST_ASSERT_TRUE(r.poll() == Action::None); /* drop tracked while armed */

  probe.up = true;
  TEST_ASSERT_TRUE(r.poll() == Action::Reinject); /* fallback trigger */
  TEST_ASSERT_EQ_INT(1, reinjector.calls);
  return 0;
}

static int test_network_blip_without_standby_does_nothing(void) {
  FakeProbe probe;
  FakeReinjector reinjector;
  Recovery r(probe, reinjector);

  probe.up = false;
  TEST_ASSERT_TRUE(r.poll() == Action::None);
  probe.up = true;
  TEST_ASSERT_TRUE(r.poll() == Action::None);
  TEST_ASSERT_EQ_INT(0, reinjector.calls);
  return 0;
}

static int test_standby_without_drop_or_resume_does_nothing(void) {
  FakeProbe probe;
  FakeReinjector reinjector;
  Recovery r(probe, reinjector);

  r.on_standby();
  probe.up = true;
  TEST_ASSERT_TRUE(r.poll() == Action::None);
  TEST_ASSERT_EQ_INT(0, reinjector.calls);
  TEST_ASSERT_TRUE(r.is_armed());
  return 0;
}

static int test_reset_clears_armed_state(void) {
  FakeProbe probe;
  FakeReinjector reinjector;
  Recovery r(probe, reinjector);

  r.on_resume();
  TEST_ASSERT_TRUE(r.is_armed());
  r.reset();
  TEST_ASSERT_TRUE(!r.is_armed());
  probe.up = true;
  TEST_ASSERT_TRUE(r.poll() == Action::None);
  TEST_ASSERT_EQ_INT(0, reinjector.calls);
  return 0;
}

}  // namespace

extern "C" int test_rest_mode_recovery_suite(void) {
  int failures = 0;
  failures += onion_test_run("rest_mode.resume_waits_for_network",
                             test_resume_waits_for_network);
  failures += onion_test_run("rest_mode.standby_drop_fallback",
                             test_standby_then_network_drop_fallback);
  failures += onion_test_run("rest_mode.blip_without_standby",
                             test_network_blip_without_standby_does_nothing);
  failures += onion_test_run("rest_mode.standby_no_drop",
                             test_standby_without_drop_or_resume_does_nothing);
  failures += onion_test_run("rest_mode.reset_clears",
                             test_reset_clears_armed_state);
  return failures;
}
