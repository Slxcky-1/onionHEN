/* Host tests for libonion_trial eval + state HMAC. */
#include "test_harness.h"

#include <stdio.h>
#include <string.h>

#include "eval.h"

static void
fill_seal(onion_beta_seal_t *seal, long long not_before, long long not_after) {
  memset(seal, 0, sizeof(*seal));
  seal->not_before = not_before;
  seal->not_after = not_after;
  seal->skew_sec = 86400;
  snprintf(seal->build_id, sizeof(seal->build_id), "%s", "beta-test");
  memset(seal->state_key, 0xab, sizeof(seal->state_key));
}

static int
fill_fp(unsigned char fp[ONION_TRIAL_DEVICE_FP_LEN]) {
  return onion_trial_device_fp("SERIAL-HOST-TEST", fp);
}

static int
test_fresh_within_window(void) {
  onion_beta_seal_t seal;
  onion_trial_state_t out;
  unsigned char fp[ONION_TRIAL_DEVICE_FP_LEN];
  int days = -1;
  long long nb = 1700000000LL;
  long long na = nb + 30LL * 86400LL;
  long long now = nb + 5LL * 86400LL;
  onion_trial_code_t code;

  fill_seal(&seal, nb, na);
  TEST_ASSERT_TRUE(fill_fp(fp) == 0);
  code = onion_trial_evaluate(&seal, now, fp, NULL, &out, &days);
  TEST_ASSERT_EQ_INT(ONION_TRIAL_OK, code);
  TEST_ASSERT_TRUE(days > 0);
  TEST_ASSERT_EQ_U64((unsigned long long)now, (unsigned long long)out.last_seen);
  TEST_ASSERT_EQ_INT(0, (int)out.sticky_expired);
  return 0;
}

static int
test_expired_sets_sticky(void) {
  onion_beta_seal_t seal;
  onion_trial_state_t out;
  unsigned char fp[ONION_TRIAL_DEVICE_FP_LEN];
  int days = 99;
  long long nb = 1700000000LL;
  long long na = nb + 30LL * 86400LL;
  long long now = na + 10;
  onion_trial_code_t code;

  fill_seal(&seal, nb, na);
  TEST_ASSERT_TRUE(fill_fp(fp) == 0);
  code = onion_trial_evaluate(&seal, now, fp, NULL, &out, &days);
  TEST_ASSERT_EQ_INT(ONION_TRIAL_EXPIRED, code);
  TEST_ASSERT_EQ_INT(0, days);
  TEST_ASSERT_EQ_INT(1, (int)out.sticky_expired);
  return 0;
}

static int
test_rollback_detected(void) {
  onion_beta_seal_t seal;
  onion_trial_state_t prior;
  onion_trial_state_t out;
  unsigned char fp[ONION_TRIAL_DEVICE_FP_LEN];
  int days = 0;
  long long nb = 1700000000LL;
  long long na = nb + 30LL * 86400LL;
  long long last = nb + 20LL * 86400LL;
  long long now = last - 2LL * 86400LL; /* > skew */
  onion_trial_code_t code;

  fill_seal(&seal, nb, na);
  TEST_ASSERT_TRUE(fill_fp(fp) == 0);
  memset(&prior, 0, sizeof(prior));
  prior.magic = ONION_TRIAL_MAGIC;
  prior.version = ONION_TRIAL_VERSION;
  snprintf(prior.build_id, sizeof(prior.build_id), "%s", seal.build_id);
  memcpy(prior.device_fp, fp, sizeof(fp));
  prior.last_seen = last;
  prior.sticky_expired = 0;

  code = onion_trial_evaluate(&seal, now, fp, &prior, &out, &days);
  TEST_ASSERT_EQ_INT(ONION_TRIAL_ROLLBACK, code);
  return 0;
}

static int
test_sticky_blocks_even_in_window(void) {
  onion_beta_seal_t seal;
  onion_trial_state_t prior;
  onion_trial_state_t out;
  unsigned char fp[ONION_TRIAL_DEVICE_FP_LEN];
  int days = 0;
  long long nb = 1700000000LL;
  long long na = nb + 30LL * 86400LL;
  long long now = nb + 5LL * 86400LL;
  onion_trial_code_t code;

  fill_seal(&seal, nb, na);
  TEST_ASSERT_TRUE(fill_fp(fp) == 0);
  memset(&prior, 0, sizeof(prior));
  prior.magic = ONION_TRIAL_MAGIC;
  prior.version = ONION_TRIAL_VERSION;
  snprintf(prior.build_id, sizeof(prior.build_id), "%s", seal.build_id);
  memcpy(prior.device_fp, fp, sizeof(fp));
  prior.last_seen = now;
  prior.sticky_expired = 1;

  code = onion_trial_evaluate(&seal, now, fp, &prior, &out, &days);
  TEST_ASSERT_EQ_INT(ONION_TRIAL_STICKY, code);
  return 0;
}

static int
test_clock_early(void) {
  onion_beta_seal_t seal;
  onion_trial_state_t out;
  unsigned char fp[ONION_TRIAL_DEVICE_FP_LEN];
  int days = 0;
  long long nb = 1700000000LL;
  long long na = nb + 30LL * 86400LL;
  long long now = nb - 2LL * 86400LL;
  onion_trial_code_t code;

  fill_seal(&seal, nb, na);
  TEST_ASSERT_TRUE(fill_fp(fp) == 0);
  code = onion_trial_evaluate(&seal, now, fp, NULL, &out, &days);
  TEST_ASSERT_EQ_INT(ONION_TRIAL_CLOCK_EARLY, code);
  return 0;
}

static int
test_hmac_roundtrip(void) {
  onion_beta_seal_t seal;
  onion_trial_state_t state;
  unsigned char fp[ONION_TRIAL_DEVICE_FP_LEN];

  fill_seal(&seal, 1000, 1000 + 30 * 86400);
  TEST_ASSERT_TRUE(fill_fp(fp) == 0);
  memset(&state, 0, sizeof(state));
  state.magic = ONION_TRIAL_MAGIC;
  state.version = ONION_TRIAL_VERSION;
  snprintf(state.build_id, sizeof(state.build_id), "%s", seal.build_id);
  memcpy(state.device_fp, fp, sizeof(fp));
  state.last_seen = 1500;
  TEST_ASSERT_TRUE(onion_trial_state_sign(&seal, &state) == 0);
  TEST_ASSERT_TRUE(onion_trial_state_verify(&seal, &state) == 0);
  state.last_seen++;
  TEST_ASSERT_TRUE(onion_trial_state_verify(&seal, &state) != 0);
  return 0;
}

static int
test_wrong_device_treated_as_fresh(void) {
  onion_beta_seal_t seal;
  onion_trial_state_t prior;
  onion_trial_state_t out;
  unsigned char fp[ONION_TRIAL_DEVICE_FP_LEN];
  unsigned char other[ONION_TRIAL_DEVICE_FP_LEN];
  int days = 0;
  long long nb = 1700000000LL;
  long long na = nb + 30LL * 86400LL;
  long long now = nb + 3LL * 86400LL;
  onion_trial_code_t code;

  fill_seal(&seal, nb, na);
  TEST_ASSERT_TRUE(fill_fp(fp) == 0);
  memset(other, 0x11, sizeof(other));
  memset(&prior, 0, sizeof(prior));
  prior.magic = ONION_TRIAL_MAGIC;
  prior.version = ONION_TRIAL_VERSION;
  snprintf(prior.build_id, sizeof(prior.build_id), "%s", seal.build_id);
  memcpy(prior.device_fp, other, sizeof(other));
  prior.last_seen = now + 10LL * 86400LL; /* would rollback if matched */
  prior.sticky_expired = 0;

  code = onion_trial_evaluate(&seal, now, fp, &prior, &out, &days);
  TEST_ASSERT_EQ_INT(ONION_TRIAL_OK, code);
  TEST_ASSERT_EQ_U64((unsigned long long)now, (unsigned long long)out.last_seen);
  return 0;
}

int
test_beta_trial_suite(void) {
  int failures = 0;
  failures += onion_test_run("beta_trial_fresh_ok", test_fresh_within_window);
  failures += onion_test_run("beta_trial_expired", test_expired_sets_sticky);
  failures += onion_test_run("beta_trial_rollback", test_rollback_detected);
  failures += onion_test_run("beta_trial_sticky", test_sticky_blocks_even_in_window);
  failures += onion_test_run("beta_trial_clock_early", test_clock_early);
  failures += onion_test_run("beta_trial_hmac", test_hmac_roundtrip);
  failures += onion_test_run("beta_trial_wrong_device",
                             test_wrong_device_treated_as_fresh);
  return failures;
}
