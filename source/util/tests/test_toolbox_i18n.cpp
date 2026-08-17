/* Host unit tests for shellui toolbox_i18n (zh-Hans / en). */
#include "test_harness.h"

#include "toolbox_i18n.hpp"
#include <onion/notify_i18n.h>

#include <cstring>
#include <string>

using namespace toolbox_i18n;

static int test_default_zh(void) {
  set_lang(Lang::ZhHans);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★OnionHEN 工具箱") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.pkg"), "内容安装与管理") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.game.sub"),
                               "当前游戏的金手指菜单") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.display"), "监控与显示") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.display.sub"),
                               "游戏覆盖层、主菜单显示与游戏选项入口") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.preferences"), "操作偏好") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("startup.open_after_load"),
                               "OnionHEN 加载后自动打开") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("startup.home_menu"), "主菜单") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.level"), "日志输出等级") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.info"), "信息（推荐）") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.trace"), "跟踪") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("pkg.installer.sub"),
                               "打开系统安装界面，用于安装 PKG 游戏或应用") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("fan.enable.sub"),
                               "关闭时使用系统默认风扇策略") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("rp.notify.countdown"),
                               "远程游玩配对剩余时间：%u 秒") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("rp.notify.paired"),
                               "远程游玩配对成功，可以开始连接") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("rp.notify.timeout"),
                               "远程游玩配对已超时") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "为 %s 启用/禁用 %s") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.game_menu"),
                               "★ OnionHEN 金手指") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("pkg.msg.options"),
                               "PKG 安装器选项") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("about.donors"), "★ 捐赠者 ★") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("about.wechat"),
                               "- 微信｜polichan01") == 0);
  return 0;
}

static int test_en(void) {
  set_lang(Lang::En);
  TEST_ASSERT_TRUE(std::strcmp(tr("root.title"), "★OnionHEN Toolbox") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.pkg"), "Content Install & Management") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.game.sub"),
                               "Cheats for the current game") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.display.sub"),
                               "In-game overlay, home menu display, and game "
                               "options entry") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.connection"),
                               "Account, Connection & Remote Play") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("group.preferences"), "Preferences") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("startup.open_after_load"),
                               "Automatically open after OnionHEN loads") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("startup.home_menu"), "Home Menu") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.level"), "Log output level") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.info"),
                               "Information (recommended)") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("log.trace"), "Trace") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("sc.off"), "Off (no shortcut)") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("remote_play.link.sub"),
                               "View PIN and account details for Remote Play "
                               "from a phone or PC") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("rp.account_id_decoded_fmt"),
                               "Decoded account ID: %s") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.enable_fmt"),
                               "Enable/disable %s for %s") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("payload.start_stop_fmt"),
                               "Start/stop %s (path: %s) (%s)") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("rp.pin_error"),
                               "Could not obtain a Remote Play PIN") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("rp.notify.countdown"),
                               "Remote Play pairing: %u seconds remaining") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("rp.notify.paired"),
                               "Remote Play paired and ready to connect") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("rp.notify.timeout"),
                               "Remote Play pairing timed out") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("debug.np_env.sub"),
                               "Change the PlayStation Network environment "
                               "string; the console reboots after saving") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("cheats.game_menu"),
                               "★ OnionHEN Cheats") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("pkg.msg.installing"),
                               "OnionHEN is installing the selected PKG") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("pkg.msg.select_all"), "Select all") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("about.donors"), "★ Donors ★") == 0);
  TEST_ASSERT_TRUE(std::strcmp(tr("about.wechat"),
                               "- WeChat | polichan01") == 0);
  return 0;
}

static int test_apply_ui_lang(void) {
  apply_ui_lang(2);
  TEST_ASSERT_TRUE(active_lang() == Lang::En);
  TEST_ASSERT_EQ_INT(2, active_ui_lang_value());
  apply_ui_lang(1);
  TEST_ASSERT_TRUE(active_lang() == Lang::ZhHans);
  TEST_ASSERT_EQ_INT(1, active_ui_lang_value());
  apply_ui_lang(99); /* invalid → zh */
  TEST_ASSERT_TRUE(active_lang() == Lang::ZhHans);
  return 0;
}

static int test_system_lang_host_fallback(void) {
  apply_system_or_ui_lang(2);
  TEST_ASSERT_TRUE(active_lang() == Lang::En);
  apply_system_or_ui_lang(0);
  TEST_ASSERT_TRUE(active_lang() == Lang::En);
  return 0;
}

static int test_missing_key(void) {
  set_lang(Lang::En);
  TEST_ASSERT_STREQ("no.such.key", tr("no.such.key"));
  return 0;
}

static int test_format(void) {
  set_lang(Lang::ZhHans);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "为 Game 启用/禁用 God");
  set_lang(Lang::En);
  TEST_ASSERT_TRUE(format("cheats.enable_fmt", "Game", "God") ==
                   "Enable/disable Game for God");
  TEST_ASSERT_TRUE(format("about.build", "v1") == "Build: v1");
  return 0;
}

extern "C" int test_toolbox_i18n_suite(void) {
  int fails = 0;
  fails += onion_test_run("i18n.default_zh", test_default_zh);
  fails += onion_test_run("i18n.en", test_en);
  fails += onion_test_run("i18n.apply_ui_lang", test_apply_ui_lang);
  fails += onion_test_run("i18n.system_lang_host_fallback",
                          test_system_lang_host_fallback);
  fails += onion_test_run("i18n.missing_key", test_missing_key);
  fails += onion_test_run("i18n.format", test_format);
  return fails;
}
