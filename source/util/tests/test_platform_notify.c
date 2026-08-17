/* Host tests for onion_notify_format (pure string path). */
#include "test_harness.h"

#include <onion/notify.h>
#include <onion/obf_str.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void format_msg(char *out, size_t out_sz, int wm, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  onion_notify_format(out, out_sz, wm, fmt, ap);
  va_end(ap);
}

static int test_notify_format_prefix(void) {
  char out[128];
  format_msg(out, sizeof(out), 1, "hello %s", "world");
  TEST_ASSERT_STREQ("[OnionHEN] hello world", out);
  return 0;
}

static int test_notify_format_truncates(void) {
  char out[16];
  format_msg(out, sizeof(out), 1, "0123456789ABCDEFGHIJ");
  /* must be NUL-terminated and start with watermark prefix */
  TEST_ASSERT_TRUE(out[sizeof(out) - 1] == '\0' || strlen(out) < sizeof(out));
  TEST_ASSERT_TRUE(strncmp(out, "[OnionHEN]", 10) == 0);
  return 0;
}

static int test_notify_format_no_watermark(void) {
  char out[128];
  format_msg(out, sizeof(out), 0, "hello %s", "world");
  TEST_ASSERT_STREQ("hello world", out);
  return 0;
}

static int test_notify_send_noop(void) {
  /* hits sceKernelSendNotificationRequest stub — must not crash */
  onion_notify(1, "host test notify %d", 7);
  onion_notify_debug("host test debug notify %d", 7);
  return 0;
}

static int test_notify_language_resolution(void) {
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ZH_HANS,
                     onion_notify_resolve_language(0, 11));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_ZH_HANS,
                     onion_notify_resolve_language(1, 1));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_EN,
                     onion_notify_resolve_language(2, 11));
  TEST_ASSERT_EQ_INT(ONION_NOTIFY_LANG_EN,
                     onion_notify_resolve_language(0, 0));
  return 0;
}

static int test_notify_format_localized(void) {
  char out[128];
  onion_notify_set_language(ONION_NOTIFY_LANG_ZH_HANS);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] 无法提升权限", out);
  onion_notify_set_language(ONION_NOTIFY_LANG_EN);
  format_msg(out, sizeof(out), 1, "notify.priv.unable");
  TEST_ASSERT_STREQ("[OnionHEN] Unable to raise privileges", out);
  return 0;
}

static int test_notify_payload_localized(void) {
  char out[256];
  onion_notify_set_language(ONION_NOTIFY_LANG_ZH_HANS);

  format_msg(out, sizeof(out), 1, "notify.payload.loading", "demo.elf");
  TEST_ASSERT_STREQ("[OnionHEN] 正在加载 Payload demo.elf...", out);

  format_msg(out, sizeof(out), 1,
             "notify.payload.launched", "/data/demo.elf",
             "demo");
  TEST_ASSERT_STREQ(
      "[OnionHEN] Payload 已启动\n路径：/data/demo.elf\n标识：demo", out);

  onion_notify_set_language(ONION_NOTIFY_LANG_EN);
  return 0;
}

static char g_rich_payload[4096];

static int32_t capture_rich_notify(int32_t user_id, bool is_logged,
                                   const char *payload) {
  TEST_ASSERT_TRUE(user_id == 0xFE);
  TEST_ASSERT_TRUE(is_logged);
  snprintf(g_rich_payload, sizeof(g_rich_payload), "%s",
           payload ? payload : "");
  return 0;
}

static int test_notify_rich_formats_payload(void) {
  g_rich_payload[0] = '\0';
  onion_notify_set_rich_send(capture_rich_notify);
  onion_notify_rich("Title", "Sub \"quoted\"", "/icon.png", "download", "42");
  TEST_ASSERT_TRUE(strstr(g_rich_payload, "InteractiveToastTemplateB") != NULL);
  TEST_ASSERT_TRUE(strstr(g_rich_payload, "\"body\": \"Title\"") != NULL);
  TEST_ASSERT_TRUE(strstr(g_rich_payload, "Sub \\\"quoted\\\"") != NULL);
  TEST_ASSERT_TRUE(strstr(g_rich_payload, "\"localNotificationId\": \"42\"") !=
                   NULL);
  return 0;
}

static int test_notify_rich_localizes_both_text_fields(void) {
  g_rich_payload[0] = '\0';
  onion_notify_set_language(ONION_NOTIFY_LANG_ZH_HANS);
  onion_notify_set_rich_send(capture_rich_notify);
  onion_notify_rich("notify.brand", "notify.boot.starting", "/icon.png",
                    "download", "43");
  TEST_ASSERT_TRUE(strstr(g_rich_payload, "OnionHEN 正在启动...") != NULL);
  onion_notify_set_language(ONION_NOTIFY_LANG_EN);
  return 0;
}

/* Same algorithm as encrypt_banner.py / obf_str.c (integrity en). */
static const uint8_t kTestIntegrityEn[] = {
    0x39, 0x57, 0x68, 0x83, 0x98, 0xb6, 0xbb, 0xd3, 0xcd, 0x2b, 0xef, 0x07,
    0x25, 0x36, 0x31, 0x93, 0x64, 0x68, 0x8b, 0x9a, 0xaa, 0xd8, 0xea, 0xc3,
    0xee, 0x02, 0x0f, 0x4f, 0x38, 0x38, 0x43, 0x67, 0x76, 0xb9, 0x9b, 0x99,
    0xf5, 0xe7, 0xd5, 0xf1, 0xf4, 0xfe, 0x1e, 0x2b, 0x45, 0x59, 0x9c, 0x78,
    0x70, 0xc9, 0xaf, 0xbd, 0xc9, 0xfd, 0xee, 0xe6, 0x11, 0x1f};

static int test_obf_decode_integrity_en(void) {
  char plain[128];
  TEST_ASSERT_EQ_INT(
      0, onion_obf_decode(kTestIntegrityEn, sizeof(kTestIntegrityEn), plain,
                          sizeof(plain)));
  TEST_ASSERT_STREQ(
      "Integrity check failed\nThis build is corrupted or modified", plain);
  return 0;
}

static int test_obf_notify_debug_helpers(void) {
  /* Must not crash; send path is stubbed on host. */
  onion_notify_set_language(ONION_NOTIFY_LANG_EN);
  onion_notify_debug_integrity_failed();
  onion_notify_debug_beta_redistrib();
  onion_notify_set_language(ONION_NOTIFY_LANG_ZH_HANS);
  onion_notify_debug_integrity_failed();
  onion_notify_debug_beta_redistrib();
  onion_notify_set_language(ONION_NOTIFY_LANG_EN);
  return 0;
}

int test_platform_notify_suite(void) {
  int failures = 0;
  failures += onion_test_run("notify_format_prefix", test_notify_format_prefix);
  failures += onion_test_run("notify_format_truncates", test_notify_format_truncates);
  failures +=
      onion_test_run("notify_format_no_watermark", test_notify_format_no_watermark);
  failures += onion_test_run("notify_send_noop", test_notify_send_noop);
  failures += onion_test_run("notify_language_resolution",
                             test_notify_language_resolution);
  failures += onion_test_run("notify_format_localized",
                             test_notify_format_localized);
  failures += onion_test_run("notify_payload_localized",
                             test_notify_payload_localized);
  failures +=
      onion_test_run("notify_rich_formats_payload", test_notify_rich_formats_payload);
  failures += onion_test_run("notify_rich_localizes_both_text_fields",
                             test_notify_rich_localizes_both_text_fields);
  failures +=
      onion_test_run("obf_decode_integrity_en", test_obf_decode_integrity_en);
  failures +=
      onion_test_run("obf_notify_debug_helpers", test_obf_notify_debug_helpers);
  return failures;
}
