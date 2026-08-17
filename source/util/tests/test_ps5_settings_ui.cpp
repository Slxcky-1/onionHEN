/* Host unit tests for shellui ps5ui::Page fluent XML builder (no PS5 SDK). */
#include "test_harness.h"

#include "ps5_settings_ui.hpp"
#include "remote_play_page_lifecycle.hpp"
#include "remote_play_pairing.hpp"

#include <string>

using namespace ps5ui;

static int test_escape_special_chars(void) {
  const std::string a = escape("a&b");
  const std::string b = escape("<tag>");
  const std::string c = escape("\"q\"");
  const std::string d = escape("/user/data/OnionHEN");
  const std::string e = escape("金手指");
  TEST_ASSERT_STREQ("a&amp;b", a.c_str());
  TEST_ASSERT_STREQ("&lt;tag&gt;", b.c_str());
  TEST_ASSERT_STREQ("&quot;q&quot;", c.c_str());
  /* escape() is for icon paths: / → // ; escape_xml keeps single / for display */
  TEST_ASSERT_STREQ("//user//data//OnionHEN", d.c_str());
  const std::string d_xml = escape_xml("/user/data/OnionHEN");
  TEST_ASSERT_STREQ("/user/data/OnionHEN", d_xml.c_str());
  /* CJK unchanged */
  TEST_ASSERT_STREQ("金手指", e.c_str());
  return 0;
}

static int test_minimal_page_shell(void) {
  const std::string xml = Page("id_root", "Root Title").build();

  TEST_ASSERT_TRUE(xml.find("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("<system_settings version=\"1.0\" "
                            "plugin=\"debug_settings_plugin\">") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("<setting_list id=\"id_root\" title=\"Root Title\">") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("</setting_list>") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("</system_settings>") != std::string::npos);
  return 0;
}

static int test_toggle_button_label_attrs(void) {
  const std::string xml =
      Page("id_page", "Page")
          .label("id_lbl", "Hello", Style::Center,
                 "/user/data/OnionHEN/assets/qr.png")
          .toggle("id_sw", "Switch", true, "second", "desc", "tex_icon")
          .button("id_btn", "Btn", "sec", "d", "icn", Style::Center)
          .build();

  TEST_ASSERT_TRUE(
      xml.find("<label id=\"id_lbl\" title=\"Hello\" "
               "icon=\"//user//data//OnionHEN//assets//qr.png\" "
               "style=\"center\"/>") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("<toggle_switch id=\"id_sw\" title=\"Switch\" "
                            "second_title=\"second\" description=\"desc\" "
                            "icon=\"tex_icon\" value=\"1\"/>") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("value=\"0\"") == std::string::npos); // only the on=true toggle
  TEST_ASSERT_TRUE(xml.find("<button id=\"id_btn\" title=\"Btn\" second_title=\"sec\" "
                            "description=\"d\" icon=\"icn\" style=\"center\"/>") !=
                   std::string::npos);
  return 0;
}

static int test_toggle_off_value(void) {
  const std::string xml =
      Page("p", "P").toggle("id_off", "Off", false).build();
  TEST_ASSERT_TRUE(xml.find("id=\"id_off\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("value=\"0\"") != std::string::npos);
  return 0;
}

static int test_list_and_items(void) {
  const std::string xml =
      Page("id_example", "Example")
          .list("id_example_list", "Choice",
                [](ListBuilder& L) {
                  L.item("id_example_list_1", "Option A", "0")
                      .item("id_example_list_2", "Option B", "1");
                },
                std::nullopt, "1")
          .button("id_example_btn", "Action", "to /data/OnionHEN/example/")
          .build();

  TEST_ASSERT_TRUE(
      xml.find("<list id=\"id_example_list\" title=\"Choice\" value=\"1\">") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      xml.find("<list_item id=\"id_example_list_1\" title=\"Option A\" "
               "value=\"0\"/>") != std::string::npos);
  TEST_ASSERT_TRUE(
      xml.find("<list_item id=\"id_example_list_2\" title=\"Option B\" "
               "value=\"1\"/>") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("</list>") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_example_btn\"") != std::string::npos);
  /* path second_title keeps single slashes */
  TEST_ASSERT_TRUE(
      xml.find("second_title=\"to /data/OnionHEN/example/\"") !=
      std::string::npos);
  return 0;
}

static int test_nested_group(void) {
  const std::string xml =
      Page("id_root", "Root")
          .group(
              "id_sub", "Sub",
              [](Group& g) {
                g.toggle("id_a", "A", true).button("id_b", "B");
              },
              "sub second")
          .build();

  TEST_ASSERT_TRUE(
      xml.find("<setting_list id=\"id_sub\" title=\"Sub\" second_title=\"sub second\">") !=
      std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_a\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_b\"") != std::string::npos);
  /* Nested close: root + sub */
  size_t closes = 0;
  for (size_t pos = 0; (pos = xml.find("</setting_list>", pos)) != std::string::npos;
       pos += 15)
    ++closes;
  TEST_ASSERT_EQ_INT(2, static_cast<int>(closes));
  return 0;
}

static int test_link_and_root_style(void) {
  const std::string xml =
      Page("id_payload", "Payload")
          .root_style(Style::Center)
          .link("id_auto_payloads", "★ 启动菜单", "auto_payloads.xml", "cfg")
          .build();

  TEST_ASSERT_TRUE(xml.find("id=\"id_payload\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("style=\"center\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("<link id=\"id_auto_payloads\" title=\"★ 启动菜单\" "
                            "second_title=\"cfg\" file=\"auto_payloads.xml\"/>") !=
                       std::string::npos ||
                   xml.find("<link id=\"id_auto_payloads\" title=\"★ 启动菜单\" "
                            "file=\"auto_payloads.xml\" second_title=\"cfg\"/>") !=
                       std::string::npos);
  return 0;
}

static int test_attr_escaping_in_titles(void) {
  const std::string xml =
      Page("id", "T & T")
          .label("l1", "a < b > c \"d\"")
          .build();

  TEST_ASSERT_TRUE(xml.find("title=\"T &amp; T\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("title=\"a &lt; b &gt; c &quot;d&quot;\"") !=
                   std::string::npos);
  return 0;
}

static int test_remote_play_like_page(void) {
  const std::string xml =
      Page("remote_play_pin_display", "远程游玩连接详情")
          .root_style(Style::Center)
          .label("id_pin", "PIN 码  : 1234 5678    ", Style::Center)
          .label("base64_account_id", "账号 ID: ABCD", Style::Center)
          .label("decoded_account_id", "解码后账号 ID: 1234ABCD",
                 Style::Center)
          .button("id_save_rp_info", "将远程游玩详情保存到 USB", std::nullopt,
                  std::nullopt, std::nullopt, Style::Center)
          .build();

  TEST_ASSERT_TRUE(xml.find("id=\"remote_play_pin_display\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_pin\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("remote_play_countdown") == std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"base64_account_id\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"decoded_account_id\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("解码后账号 ID: 1234ABCD") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_save_rp_info\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("远程游玩") != std::string::npos);
  return 0;
}

static int test_remote_play_countdown_rounding(void) {
  using remote_play::seconds_remaining;
  TEST_ASSERT_EQ_INT(120, seconds_remaining(120000, 0));
  TEST_ASSERT_EQ_INT(120, seconds_remaining(120000, 1));
  TEST_ASSERT_EQ_INT(1, seconds_remaining(120000, 119001));
  TEST_ASSERT_EQ_INT(0, seconds_remaining(120000, 120000));
  TEST_ASSERT_EQ_INT(0, seconds_remaining(120000, 120001));
  TEST_ASSERT_EQ_INT(120, seconds_remaining(999999, 0));
  return 0;
}

static int test_remote_play_countdown_notification_marks(void) {
  using remote_play::countdown_notification_mark;
  TEST_ASSERT_EQ_INT(120, countdown_notification_mark(121));
  TEST_ASSERT_EQ_INT(120, countdown_notification_mark(120));
  TEST_ASSERT_EQ_INT(120, countdown_notification_mark(111));
  TEST_ASSERT_EQ_INT(110, countdown_notification_mark(110));
  TEST_ASSERT_EQ_INT(110, countdown_notification_mark(101));
  TEST_ASSERT_EQ_INT(100, countdown_notification_mark(100));
  TEST_ASSERT_EQ_INT(10, countdown_notification_mark(1));
  TEST_ASSERT_EQ_INT(0, countdown_notification_mark(0));
  return 0;
}

static int test_remote_play_page_lifecycle(void) {
  using remote_play::PageObservation;
  using remote_play::PageObservationAction;
  using remote_play::PagePhase;
  using remote_play::classify_page_observation;

  TEST_ASSERT_TRUE(classify_page_observation(
                       PagePhase::Loading, {false, true}) ==
                   PageObservationAction::Wait);
  TEST_ASSERT_TRUE(classify_page_observation(
                       PagePhase::Loading, {true, true}) ==
                   PageObservationAction::MarkVisible);
  TEST_ASSERT_TRUE(classify_page_observation(
                       PagePhase::Visible, {true, true}) ==
                   PageObservationAction::StayVisible);
  TEST_ASSERT_TRUE(classify_page_observation(
                       PagePhase::Visible, {false, false}) ==
                   PageObservationAction::LeaveToPrevious);
  TEST_ASSERT_TRUE(classify_page_observation(
                       PagePhase::Visible, {false, true}) ==
                   PageObservationAction::LeaveToolbox);
  TEST_ASSERT_TRUE(classify_page_observation(
                       PagePhase::BackRequested, {true, true}) ==
                   PageObservationAction::StayVisible);
  return 0;
}

static int test_fluent_returns_this(void) {
  Page page("p", "P");
  Page& chain = page.toggle("t1", "T1", false).button("b1", "B1").label("l1", "L1");
  TEST_ASSERT_TRUE(&chain == &page);
  const std::string xml = page.build();
  TEST_ASSERT_TRUE(xml.find("id=\"t1\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"b1\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"l1\"") != std::string::npos);
  return 0;
}

static int test_text_field_and_confirm(void) {
  const std::string xml =
      Page("id_page", "Page")
          .text_field("id_fan_speed", "调整风扇阈值", "按摄氏度调整风扇阈值",
                      "number", "2", "2", std::nullopt, std::nullopt,
                      std::nullopt, "55")
          .text_field("id_np_env", "NP 环境", std::nullopt, "basic_latin", "1",
                      "16", "/NP/env", "系统将重启以应用此设置。", "确定,取消")
          .toggle("id_disp_titleids", "显示 Title ID", false, "second")
          .list("id_cheats_shortcut", "打开金手指菜单",
                [](ListBuilder& L) {
                  L.item("id_cheats_shortcut_0", "关闭（无快捷键）", "0");
                },
                std::nullopt, "0")
          .label("id_left", "left text", Style::Left)
          .build();

  TEST_ASSERT_TRUE(xml.find("<text_field id=\"id_fan_speed\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("keyboard_type=\"number\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("min_length=\"2\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("max_length=\"2\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("value=\"55\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("key=\"/NP/env\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("confirm_phrase=\"确定,取消\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("confirm=\"系统将重启以应用此设置。\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_cheats_shortcut\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("style=\"left\"") != std::string::npos);
  return 0;
}

static int test_toolbox_like_skeleton(void) {
  const std::string xml =
      Page("id_debug_settings", "★OnionHEN 工具箱")
          .root_focus("id_group_pkg")
          .group(
              "id_group_pkg", "内容安装与管理",
              [](Group& g) {
                g.link("id_game_package_installer", "软件包安装器",
                       "PkgInstaller/data/pkginstaller.xml");
              },
              "安装 PKG 与管理附加内容",
              "/user/data/OnionHEN/assets/icon_xml_package.png",
              "id_game_package_installer")
          .build();

  TEST_ASSERT_TRUE(xml.find("id=\"id_debug_settings\"") != std::string::npos);
  TEST_ASSERT_TRUE(xml.find("initial_focus_to=\"id_group_pkg\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(xml.find("id=\"id_group_pkg\"") != std::string::npos);
  /* icon doubles slashes; file (plugin resource) keeps single / */
  TEST_ASSERT_TRUE(
      xml.find("icon=\"//user//data//OnionHEN//assets//icon_xml_package.png\"") !=
      std::string::npos);
  TEST_ASSERT_TRUE(xml.find("file=\"PkgInstaller/data/pkginstaller.xml\"") !=
                   std::string::npos);
  return 0;
}

static int test_icons_on_every_entry_kind(void) {
  const char* icon = "/user/data/icon.png";
  const std::string escaped_icon = "icon=\"//user//data//icon.png\"";
  const std::string xml =
      Page("id_root", "Root")
          .root_icon(icon)
          .group(
              "id_group", "Group",
              [icon](Group& g) {
                g.label("id_label", "Label", Style::None, icon)
                    .toggle("id_toggle", "Toggle", false, std::nullopt,
                            std::nullopt, icon)
                    .button("id_button", "Button", std::nullopt,
                            std::nullopt, icon)
                    .link("id_link", "Link", "page.xml", std::nullopt, icon)
                    .list(
                        "id_list", "List",
                        [icon](ListBuilder& list) {
                          list.item("id_item", "Item", "1", icon);
                        },
                        std::nullopt, "1", std::nullopt, std::nullopt, icon)
                    .text_field("id_text", "Text", std::nullopt,
                                std::nullopt, std::nullopt, std::nullopt,
                                std::nullopt, std::nullopt, std::nullopt,
                                std::nullopt, icon);
              },
              std::nullopt, icon)
          .build();

  const char* ids[] = {"id_root",   "id_group", "id_label", "id_toggle",
                       "id_button", "id_link",  "id_list",  "id_item",
                       "id_text"};
  for (const char* id : ids) {
    const size_t node = xml.find(std::string("id=\"") + id + "\"");
    TEST_ASSERT_TRUE(node != std::string::npos);
    const size_t end = xml.find('>', node);
    TEST_ASSERT_TRUE(end != std::string::npos);
    TEST_ASSERT_TRUE(xml.substr(node, end - node).find(escaped_icon) !=
                     std::string::npos);
  }
  return 0;
}

extern "C" int test_ps5_settings_ui_suite(void) {
  int fails = 0;
  fails += onion_test_run("ps5ui.escape", test_escape_special_chars);
  fails += onion_test_run("ps5ui.minimal_page", test_minimal_page_shell);
  fails += onion_test_run("ps5ui.widgets", test_toggle_button_label_attrs);
  fails += onion_test_run("ps5ui.toggle_off", test_toggle_off_value);
  fails += onion_test_run("ps5ui.list", test_list_and_items);
  fails += onion_test_run("ps5ui.nested_group", test_nested_group);
  fails += onion_test_run("ps5ui.link_style", test_link_and_root_style);
  fails += onion_test_run("ps5ui.attr_escape", test_attr_escaping_in_titles);
  fails += onion_test_run("ps5ui.remote_play_like", test_remote_play_like_page);
  fails += onion_test_run("ps5ui.remote_play_countdown",
                          test_remote_play_countdown_rounding);
  fails += onion_test_run("ps5ui.remote_play_notify_marks",
                          test_remote_play_countdown_notification_marks);
  fails += onion_test_run("ps5ui.remote_play_page_lifecycle",
                          test_remote_play_page_lifecycle);
  fails += onion_test_run("ps5ui.fluent_chain", test_fluent_returns_this);
  fails += onion_test_run("ps5ui.text_field_confirm", test_text_field_and_confirm);
  fails += onion_test_run("ps5ui.toolbox_skeleton", test_toolbox_like_skeleton);
  fails += onion_test_run("ps5ui.icons_everywhere",
                          test_icons_on_every_entry_kind);
  return fails;
}
