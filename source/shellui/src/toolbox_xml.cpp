/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Extracted from mono_utils.cpp for module locality.
 * Dynamic settings pages are built via ps5ui::Page (fluent XML builder).
 */

#include "hooked_funcs.hpp"
#include "remote_play.h"
#include "defs.h"
#include "external_symbols.hpp"
#include "ipc.hpp"
#include "ps5_settings_ui.hpp"
#include "toolbox_i18n.hpp"
#include "toolbox_values.hpp"
#include "onion_cjson.hpp"

#define PIN_CODE_SIZE 64
#define ACCOUNT_ID_BASE64_SIZE 16

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <vector>
#include <string>

#include "shellui_state.hpp"
#include "toolbox_helpers.hpp"

void escapeXML(std::string& input);
bool Get_Running_App_TID(std::string& title_id, int& BigAppid);

void escapeXML(std::string& input) {
  input = ps5ui::escape(input);
}

namespace {

#if defined(ONION_ENABLE_BETA_TRIAL)
/*
 * Beta banner format strings (zh/en) are not plain C string literals.
 * Blob: enc[i] = (plain[i] ^ key[i%k]) + (i*17 + 31)  (mod 256)
 * Key:  base64(kBannerXorKeyB64), same material as prx version banner.
 * Regenerate: python3 source/shellui/assets/encrypt_banner.py
 */
constexpr const char *kBannerXorKeyB64 = "U0lTVFIwX0lfU0VFX1lPVQ==";

/* Obfuscated zh-Hans format (printf %s = ONIONHEN_VERSION). */
constexpr unsigned char kBetaBannerZhEnc[] = {
    0xd0, 0x01, 0x17, 0xc6, 0x1a, 0x2a, 0x5f, 0x45, 0x91, 0x90, 0x2e, 0x3a,
    0x17, 0x75, 0x9a, 0x00, 0xa2, 0xee, 0x46, 0x37, 0x27, 0x21, 0x92, 0x52,
    0xb2, 0x8d, 0x7a, 0xe3, 0xfa, 0x85, 0xaa, 0x10, 0xb2, 0xf1, 0x39, 0x63,
    0x39, 0x1f, 0x6c, 0x57, 0xb2, 0xc2, 0x96, 0xeb, 0xfd, 0xd9, 0x23, 0x23,
    0x0a, 0x46, 0x55, 0x3f, 0x65, 0x54, 0x6e, 0xab, 0xb8, 0x5b, 0xa0, 0xe7,
    0xf5};

/* Obfuscated en-US format (printf %s = ONIONHEN_VERSION). */
constexpr unsigned char kBetaBannerEnEnc[] = {
    0xd0, 0x01, 0x17, 0xc6, 0x73, 0xe9, 0x90, 0x9e, 0x26, 0x2e, 0xff, 0x3f,
    0x88, 0xea, 0x7c, 0x2f, 0x4b, 0xa9, 0x6e, 0x7d, 0x79, 0x94, 0xa2, 0xb2,
    0xd2, 0xe2, 0xef, 0xfb, 0x08, 0x1c, 0x2a, 0x2e, 0x46, 0x5c, 0xd4, 0x08,
    0x68, 0xa4, 0xbb, 0xe5, 0x46, 0xfb, 0x0d, 0x26, 0x46, 0x91, 0x9c, 0x65,
    0x85, 0x98, 0x97, 0xb3, 0xb4, 0xe8, 0x34, 0xee, 0x56, 0x09, 0x19, 0x2d,
    0x45, 0x63, 0x68, 0xc3, 0x10, 0x41, 0x57};

std::string decode_beta_banner_fmt(const unsigned char *enc, size_t n) {
  const std::string key = base64_decode(kBannerXorKeyB64);
  if (key.empty() || n == 0)
    return {};
  std::string out(n, '\0');
  for (size_t i = 0; i < n; ++i) {
    unsigned char b = enc[i];
    b = static_cast<unsigned char>(b -
                                   static_cast<unsigned char>(i * 17u + 31u));
    b ^= static_cast<unsigned char>(key[i % key.size()]);
    out[i] = static_cast<char>(b);
  }
  return out;
}
#endif

/** Payload .elf only (OnionHEN no longer supports .plugin packages). */
template <typename G>
void append_payload_entry(G& page, const std::string& directory, const char* filename,
                          bool list_page, int& next_id) {
  if (!toolbox::is_payload_elf_name(filename))
    return;

  const std::string path = directory + "/" + filename;
  char elf_key[64] = {};
  if (!toolbox::elf_key_from_name(filename, elf_key, sizeof(elf_key))) {
    LOG_ERROR("Skipping invalid payload name: %s", filename);
    return;
  }

  /* Confirm file is readable (ELF magic checked at launch). */
  const int fd = open(path.c_str(), O_RDONLY, 0);
  if (fd < 0) {
    LOG_ERROR("Failed to open payload: %s", path.c_str());
    return;
  }
  close(fd);

  LOG_DEBUG("Found payload: %s key=%s", path.c_str(), elf_key);

  const std::string shown_path = toolbox::display_path_for_ui(path);
  const std::string id_prefix = list_page ? "id_payload_" : "id_auto_payload_";
  const std::string id = id_prefix + std::to_string(next_id++);

  std::string second;
  if (list_page) {
    second = std::string(toolbox_i18n::tr("payload.start_stop")) + filename +
             toolbox_i18n::tr("payload.path") + shown_path + ") (" + elf_key +
             ")";
  } else {
    second = std::string(toolbox_i18n::tr("payload.autostart_enable")) +
             filename + toolbox_i18n::tr("payload.autostart_suffix") +
             shown_path + ")";
  }

  page.toggle(id, filename, /*on=*/false, second);

  PayloadEntry entry;
  entry.shellui_path = path;
  entry.tid = elf_key;
  entry.path = shown_path;
  entry.name = filename;
  entry.version = "";
  entry.id = id;
  if (list_page)
    g_ui.payloads_list.push_back(entry);
  else
    g_ui.auto_payloads_list.push_back(entry);
}

template <typename G>
void append_homebrew_game(G& page, const std::string& game_dir, const char* dir_name,
                          int random_num) {
  const std::string elf_path = game_dir + "/eboot.elf";
  if (access(elf_path.c_str(), F_OK) != 0)
    return;

#if SHELL_DEBUG == 1
  LOG_DEBUG("Found Game: %s", game_dir.c_str());
#endif

  std::string title_id, title, ver;
  const std::string shown_path = toolbox::display_path_for_ui(game_dir);
  const std::string icon_path = game_dir + "/sce_sys/icon0.png";

  GameEntry game;
  game.tid = title_id;
  game.title = title;
  game.version = ver;
  game.path = shown_path;
  game.dir_name = dir_name;
  game.icon_path = icon_path;
  game.id = "id_onionhen_pl_loader_" + title_id + "_" + std::to_string(random_num);
  g_ui.games_list.push_back(game);

  page.button(game.id, "(" + title_id + ") " + title,
              shown_path + toolbox_i18n::tr("plapps.version") + ver,
              std::nullopt, icon_path);
}

std::string read_file_to_string(const char* path) {
  struct stat st {};
  if (stat(path, &st) == -1 || st.st_size <= 0) {
    LOG_ERROR("Unable to stat file %s", path);
    return {};
  }

  const int fd = open(path, O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("Error reading %s file!", path);
    return {};
  }

  std::string buf(static_cast<size_t>(st.st_size), '\0');
  const ssize_t n = read(fd, buf.data(), buf.size());
  close(fd);

  if (n < 0 || static_cast<size_t>(n) != buf.size()) {
    LOG_ERROR("read failed for %s", path);
    return {};
  }
  return buf;
}

std::string join_authors(cJSON* root) {
  std::unordered_set<std::string> seen;
  std::string joined;
  bool first = true;
  cJSON* authors = onion_cjson::item(root, "authors");
  if (!cJSON_IsArray(authors))
    return joined;

  cJSON* author = nullptr;
  cJSON_ArrayForEach(author, authors) {
    const char* value = onion_cjson::string_value(author);
    if (!value || !seen.insert(value).second)
      continue;
    if (!first)
      joined += ", ";
    joined += value;
    first = false;
  }
  return joined;
}

template <typename G>
void append_cheat_entries(G& page, cJSON* root, const std::string& tid,
                          const std::string& game_name, bool can_toggle) {
  cJSON* cheats = onion_cjson::item(root, "cheats");
  if (!cJSON_IsArray(cheats))
    return;

  cJSON* entry = nullptr;
  cJSON_ArrayForEach(entry, cheats) {
    const std::string name = onion_cjson::string_item(entry, "name", "");
    std::string desc =
        onion_cjson::string_item(entry, "description", "");
    if (desc.empty())
      desc = toolbox_i18n::tr("cheats.on_off");
    const int id = onion_cjson::int_item(entry, "id");
    const bool enabled = onion_cjson::bool_item(entry, "enabled");
    const std::string id_attr = "id_cheat_" + tid + "_" + std::to_string(id);

    if (can_toggle) {
      page.toggle(id_attr, name, enabled, std::nullopt, desc, "tex_game_icon");
    } else {
      page.button(id_attr, name,
                  std::string(toolbox_i18n::tr("cheats.enable_for")) +
                      game_name + toolbox_i18n::tr("cheats.enable_mid") + name,
                  desc, "tex_game_icon");
    }
    g_ui.set_cheat_enabled(id, enabled);
  }
}

} // namespace

void generate_remote_play_xml(std::string& xml_buffer) {
  char pin_code[PIN_CODE_SIZE] = {0};
  char AccountID[ACCOUNT_ID_BASE64_SIZE] = {0};
  uint64_t dec_account_id = 0;
  bool activated_now = false;
  bzero(AccountID, ACCOUNT_ID_BASE64_SIZE);

  LOG_DEBUG("Starting remote play");
  static bool remote_play_initialized = false;
  if (!remote_play_initialized) {
    remote_play_initialized = InitRemotePlay();
  }

  ps5ui::Page page("remote_play_pin_display", toolbox_i18n::tr("rp.title"));
  page.root_style(ps5ui::Style::Center);

  if (!remote_play_initialized) {
    page.label("remote_play_init_error", toolbox_i18n::tr("rp.init_error"),
               ps5ui::Style::Center);
    xml_buffer = page.build();
    return;
  }

  if (!GetEncodedAccountID(AccountID, dec_account_id, activated_now)) {
    page.label("remote_play_account_error",
               toolbox_i18n::tr("rp.account_error"), ps5ui::Style::Center);
    xml_buffer = page.build();
    return;
  }

  if (activated_now) {
    page.label("id_pin_2", toolbox_i18n::tr("rp.need_reboot"),
               ps5ui::Style::Center);
    xml_buffer = page.build();
    return;
  }

  LOG_DEBUG("Get encoded account id ==> %s", AccountID);

  std::stringstream account_id_stream;
  account_id_stream << std::hex << std::uppercase << dec_account_id;
  const std::string decoded_account_id = account_id_stream.str();

  g_ui.remote_play_info =
      std::string(toolbox_i18n::tr("rp.account_id")) + AccountID;
  g_ui.remote_play_info +=
      std::string("\n") + toolbox_i18n::tr("rp.account_id_decoded") +
      decoded_account_id;

  uint32_t pinCode = 0;
  const bool pin_ready = GeneratePINCode(pinCode);
  std::string pin_display;
  if (pin_ready) {
    LOG_DEBUG("Pin code => %u", pinCode);
    snprintf(pin_code, sizeof(pin_code), "%s%04u %04u    ",
             toolbox_i18n::tr("rp.pin"), pinCode / 10000u,
             pinCode % 10000u);
    pin_display = pin_code;
    LOG_DEBUG("Pin code str => %s", pin_code);
  } else {
    pin_display = toolbox_i18n::tr("rp.pin_error");
  }
  g_ui.remote_play_info += "\n" + pin_display;

  page.label("id_pin", pin_display, ps5ui::Style::Center)
      .label("base64_account_id",
             std::string(toolbox_i18n::tr("rp.account_id")) + AccountID,
             ps5ui::Style::Center)
      .label("decoded_account_id",
             std::string(toolbox_i18n::tr("rp.account_id_decoded")) +
                 decoded_account_id,
             ps5ui::Style::Center);

  if (usbpath() != -1)
    page.button("id_save_rp_info", toolbox_i18n::tr("rp.save_usb"),
                toolbox_i18n::tr("rp.save_usb.sub"), std::nullopt, std::nullopt,
                ps5ui::Style::Center);

  xml_buffer = page.build();
}

void generate_payload_xml(std::string& xml_buffer, bool list_page) {
  static const std::vector<std::string> kPayloadDirs = {
      "/user/data/OnionHEN/payloads",
      "/data/OnionHEN/payloads",
      "/usb0/OnionHEN/payloads",
      "/usb1/OnionHEN/payloads",
      "/usb2/OnionHEN/payloads",
      "/usb3/OnionHEN/payloads",
  };

  const char* root_id = list_page ? "id_payload" : "id_auto_payloads";
  const char* root_title =
      list_page ? toolbox_i18n::tr("payload.title")
                : toolbox_i18n::tr("payload.auto_title");
  ps5ui::Page page(root_id, root_title);

  int toggle_switch_id = 1;
  for (const auto& directory : kPayloadDirs) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
      LOG_ERROR("Failed to open directory: %s", directory.c_str());
      continue;
    }
    while (struct dirent* entry = readdir(dir))
      append_payload_entry(page, directory, entry->d_name, list_page,
                           toggle_switch_id);
    closedir(dir);
  }

  xml_buffer = page.build();
}

void generate_cheats_xml(std::string& new_xml, std::string& not_open_tid,
                         bool running_as_debug_settings, bool show_while_not_open) {
  const std::string list_id =
      running_as_debug_settings ? "id_debug_settings" : "id_cheat_title";

  int appid = -1;
  g_ui.is_game_open = Get_Running_App_TID(g_ui.running_tid, appid);
  g_ui.is_current_game_open =
      g_ui.is_game_open &&
      g_ui.running_tid == (show_while_not_open ? not_open_tid : g_ui.running_tid);

  if (!g_ui.is_game_open && !show_while_not_open) {
    ps5ui::Page page(list_id, toolbox_i18n::tr("cheats.none"));
    page.label("id_cheat_no_game", toolbox_i18n::tr("cheats.none.hint"),
               ps5ui::Style::Center);
    new_xml = page.build();
    return;
  }

  g_ui.running_tid = show_while_not_open ? not_open_tid : g_ui.running_tid;
  IPC_Client& client = IPC_Client::getInstance(true);

  std::string game_ver;
  if (!client.GameVerFromTid(g_ui.running_tid, game_ver))
    game_ver = toolbox_i18n::tr("cheats.ver_unknown");

  ps5ui::Page page(list_id, std::string(toolbox_i18n::tr("cheats.title_prefix")) +
                                g_ui.running_tid + " - " + game_ver);

  if (!g_ui.is_game_open && show_while_not_open) {
    page.label("id_cheat_disclaimer",
               g_ui.running_tid + toolbox_i18n::tr("cheats.not_running"),
               ps5ui::Style::Center);
  }

  std::string cheat_path;
  if (!client.GetGameCheats(g_ui.running_tid, game_ver, cheat_path)) {
    page.label("id_cheat_missing", toolbox_i18n::tr("cheats.missing"),
               ps5ui::Style::Center);
    new_xml = page.build();
    return;
  }

  const std::string json_string = read_file_to_string(cheat_path.c_str());
  if (json_string.empty()) {
    new_xml = page.build();
    return;
  }
  unlink(cheat_path.c_str());

  onion_cjson::Root res_json(json_string);
  if (!res_json) {
    LOG_ERROR("Failed to parse json from cheat response!");
    new_xml = page.build();
    return;
  }

  const std::string game_name =
      onion_cjson::string_item(res_json.get(), "name", "");
  page.label("id_cheat_title", "★ " + game_name + " ★", ps5ui::Style::Center);

  const std::string authors = join_authors(res_json.get());
  page.label("credits",
             std::string(toolbox_i18n::tr("cheats.authors")) + authors,
             ps5ui::Style::Center);

  append_cheat_entries(page, res_json.get(), g_ui.running_tid, game_name,
                       g_ui.is_game_open && g_ui.is_current_game_open);
  new_xml = page.build();
}

void generate_plapps_xml(std::string& new_xml) {
  static const std::vector<std::string> kHomebrewDirs = {
      "/user/data/homebrew/games",
      "/usb0/homebrew",
      "/usb1/homebrew/games",
      "/usb2/homebrew/games",
      "/usb3/homebrew/games",
      "/mnt/ext1/homebrew/games",
      "/mnt/ext2/homebrew/games",
      "/mnt/ext0/homebrew/games",
  };

  ps5ui::Page page("id_plapps", toolbox_i18n::tr("plapps.title"));

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(1000, 9999);

  for (const auto& directory : kHomebrewDirs) {
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
#if SHELL_DEBUG == 1
      LOG_ERROR("Failed to open directory: %s", directory.c_str());
#endif
      continue;
    }

    while (struct dirent* entry = readdir(dir)) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;

      const std::string game_dir = directory + "/" + entry->d_name;
      struct stat st {};
      if (stat(game_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
#if SHELL_DEBUG == 1
        LOG_WARN("Skipping non-directory: %s", game_dir.c_str());
#endif
        continue;
      }

      append_homebrew_game(page, game_dir, entry->d_name, dist(gen));
    }
    closedir(dir);
  }

  new_xml = page.build();
}

namespace {

constexpr const char* kIconPkg =
    "/user/data/OnionHEN/assets/icon_xml_package.png";
constexpr const char* kIconPlugins =
    "/user/data/OnionHEN/assets/icon_xml_plugins.png";
constexpr const char* kIconGame = "/user/data/OnionHEN/assets/icon_xml_game.png";
constexpr const char* kIconMonitor =
    "/user/data/OnionHEN/assets/icon_xml_monitor.png";
constexpr const char* kIconAccount =
    "/user/data/OnionHEN/assets/icon_xml_account.png";
constexpr const char* kIconSettings =
    "/user/data/OnionHEN/assets/icon_xml_settings.png";
constexpr const char* kIconShortcuts =
    "/user/data/OnionHEN/assets/icon_xml_shortcuts.png";
constexpr const char* kIconDebug =
    "/user/data/OnionHEN/assets/icon_xml_debug.png";
constexpr const char* kIconAbout =
    "/user/data/OnionHEN/assets/icon_xml_about.png";
constexpr const char* kIconKstuff =
    "/user/data/OnionHEN/assets/icon_xml_kstuff.png";
constexpr const char* kIconOverlay =
    "/user/data/OnionHEN/assets/icon_xml_overlay.png";
constexpr const char* kIconTitleId =
    "/user/data/OnionHEN/assets/icon_xml_title_id.png";
constexpr const char* kIconMenuOption =
    "/user/data/OnionHEN/assets/icon_xml_menu_option.png";
constexpr const char* kIconFan =
    "/user/data/OnionHEN/assets/icon_xml_fan.png";
constexpr const char* kIconRestMode =
    "/user/data/OnionHEN/assets/icon_xml_restmode.png";
constexpr const char* kIconHardDrive =
    "/user/data/OnionHEN/assets/icon_xml_hardrive.png";
constexpr const char* kIconDiscLicense =
    "/user/data/OnionHEN/assets/icon_xml_disc_license.png";
constexpr const char* kIconRemotePlay =
    "/user/data/OnionHEN/assets/icon_xml_remote_play.png";
constexpr const char* kIconDonations =
    "/user/data/OnionHEN/assets/icon_xml_donations.png";
constexpr const char* kIconThanks =
    "/user/data/OnionHEN/assets/icon_xml_thanks.png";
constexpr const char* kIconProject =
    "/user/data/OnionHEN/assets/icon_xml_project.png";
constexpr const char* kIconAuthorAvatar =
    "/user/data/OnionHEN/assets/icon_xml_author_avatar.png";
constexpr const char* kIconDonatorLjf =
    "/user/data/OnionHEN/assets/icon_xml_donator_ljf.png";
constexpr const char* kIconDonatorSzx =
    "/user/data/OnionHEN/assets/icon_xml_donator_szx.png";
constexpr const char* kIconDonatorAglx =
    "/user/data/OnionHEN/assets/icon_xml_donator_aglx.png";

bool toolbox_on(const char* id) {
  return resolve_toolbox_control_value(id) == "1";
}

std::string toolbox_val(const char* id, const char* fallback = "0") {
  std::string v = resolve_toolbox_control_value(id);
  return v.empty() ? fallback : v;
}

void append_toolbox_pkg_group(ps5ui::Group& g) {
  g.link("id_game_package_installer", toolbox_i18n::tr("pkg.installer"),
         "PkgInstaller/data/pkginstaller.xml",
         toolbox_i18n::tr("pkg.installer.sub"))
      .link("id_game_add_content_manager", toolbox_i18n::tr("pkg.add_content"),
            "Addcontent/data/addcontent.xml",
            toolbox_i18n::tr("pkg.add_content.sub"));
}

void append_toolbox_payloads_group(ps5ui::Group& g) {
  g.link("id_payloads", toolbox_i18n::tr("payloads.link"), "payloads.xml",
         toolbox_i18n::tr("payloads.link.sub"), kIconPlugins)
      .link("id_auto_payloads", toolbox_i18n::tr("payload.auto.link"),
            "auto_payloads.xml", toolbox_i18n::tr("payload.auto.sub"), kIconPlugins)
      .group(
          "id_kstuff_opts", toolbox_i18n::tr("kstuff.group"),
          [](ps5ui::Group& k) {
            k.toggle("id_kstuff_autoload", toolbox_i18n::tr("kstuff.autoload"),
                     toolbox_on("id_kstuff_autoload"),
                     toolbox_i18n::tr("kstuff.autoload.sub"))
                .button("id_delete_kstuff", toolbox_i18n::tr("kstuff.delete"),
                        std::nullopt, toolbox_i18n::tr("kstuff.delete.desc"));
          },
          toolbox_i18n::tr("kstuff.group.sub"), kIconKstuff,
          "id_kstuff_autoload");
}

void append_toolbox_game_group(ps5ui::Group& g) {
  g.link("id_cheats", toolbox_i18n::tr("cheats.link"), "cheats.xml",
         toolbox_i18n::tr("cheats.link.sub"));
}

void append_toolbox_display_group(ps5ui::Group& g) {
  g.group(
       "id_overlay_opts", toolbox_i18n::tr("overlay.group"),
       [](ps5ui::Group& o) {
         o.toggle("id_overlay_enabled", toolbox_i18n::tr("overlay.enabled"),
                  toolbox_on("id_overlay_enabled"), std::nullopt,
                  toolbox_i18n::tr("overlay.enabled.desc"))
             .list("id_overlay_change_pos", toolbox_i18n::tr("overlay.pos"),
                   [](ps5ui::ListBuilder& L) {
                     L.item("id_overlay_pos_1",
                            toolbox_i18n::tr("overlay.pos.top"), "0")
                         .item("id_overlay_pos_3",
                               toolbox_i18n::tr("overlay.pos.bottom"), "2");
                   },
                   toolbox_i18n::tr("overlay.pos.sub"),
                   toolbox_val("id_overlay_change_pos"))
             .toggle("id_overlay_gpu", toolbox_i18n::tr("overlay.gpu"),
                     toolbox_on("id_overlay_gpu"), std::nullopt,
                     toolbox_i18n::tr("overlay.gpu.desc"))
             .toggle("id_overlay_cpu", toolbox_i18n::tr("overlay.cpu"),
                     toolbox_on("id_overlay_cpu"), std::nullopt,
                     toolbox_i18n::tr("overlay.cpu.desc"))
             .toggle("id_all_cpu_usage", toolbox_i18n::tr("overlay.cpu_all"),
                     toolbox_on("id_all_cpu_usage"), std::nullopt,
                     toolbox_i18n::tr("overlay.cpu_all.desc"))
             .toggle("id_overlay_ram", toolbox_i18n::tr("overlay.ram"),
                     toolbox_on("id_overlay_ram"), std::nullopt,
                     toolbox_i18n::tr("overlay.ram.desc"))
             .toggle("id_overlay_ip", toolbox_i18n::tr("overlay.ip"),
                     toolbox_on("id_overlay_ip"), std::nullopt,
                     toolbox_i18n::tr("overlay.ip.desc"));
       },
       toolbox_i18n::tr("overlay.group.sub"), kIconOverlay,
       "id_overlay_enabled")
      .toggle("id_disp_titleids", toolbox_i18n::tr("disp_tids"),
              toolbox_on("id_disp_titleids"), toolbox_i18n::tr("disp_tids.sub"),
              std::nullopt, kIconTitleId)
      .toggle("id_custom_game_opts", toolbox_i18n::tr("game_opts.toggle"),
              toolbox_on("id_custom_game_opts"),
              toolbox_i18n::tr("game_opts.toggle.sub"), std::nullopt,
              kIconMenuOption);
}

void append_toolbox_connection_group(ps5ui::Group& g) {
  g.link("remote_play", toolbox_i18n::tr("remote_play.link"),
         "remote_play.xml", toolbox_i18n::tr("remote_play.link.sub"),
         kIconRemotePlay);
}

void append_toolbox_system_group(ps5ui::Group& g) {
  g.group(
       "id_group_fan", toolbox_i18n::tr("fan.group"),
       [](ps5ui::Group& f) {
         f.toggle("id_enable_fan_speed", toolbox_i18n::tr("fan.enable"),
                  toolbox_on("id_enable_fan_speed"),
                  toolbox_i18n::tr("fan.enable.sub"))
             .text_field("id_fan_speed", toolbox_i18n::tr("fan.threshold"),
                         toolbox_i18n::tr("fan.threshold.sub"), "number", "2",
                         "2", std::nullopt, std::nullopt, std::nullopt,
                         toolbox_val("id_fan_speed", ""));
       },
       toolbox_i18n::tr("fan.group.sub"), kIconFan, "id_enable_fan_speed")
      .group(
          "id_rest_mode", toolbox_i18n::tr("rest.group"),
          [](ps5ui::Group& r) {
            r.text_field("id_rest_1", toolbox_i18n::tr("rest.delay"),
                         toolbox_i18n::tr("rest.delay.sub"), "number", "1",
                         "255", std::nullopt, std::nullopt, std::nullopt,
                         toolbox_val("id_rest_1", ""));
          },
          toolbox_i18n::tr("rest.group.sub"), kIconRestMode, "id_rest_1")
      .link("id_external_hdd", toolbox_i18n::tr("hdd.external"),
            "DebugSettings/data/debug_settings_external_hdd.xml",
            toolbox_i18n::tr("hdd.external.sub"), kIconHardDrive)
      .link("id_licenseactivation", toolbox_i18n::tr("license.bd"),
            "DebugSettings/data/debug_settings_licenseactivation.xml",
            toolbox_i18n::tr("license.bd.sub"), kIconDiscLicense);
}

void append_toolbox_preferences_group(ps5ui::Group& g) {
  g.list("id_ui_lang", toolbox_i18n::tr("lang.list"),
         [](ps5ui::ListBuilder& L) {
           L.item("id_ui_lang_system", toolbox_i18n::tr("lang.system"), "0")
               .item("id_ui_lang_zh", toolbox_i18n::tr("lang.zh"), "1")
               .item("id_ui_lang_en", toolbox_i18n::tr("lang.en"), "2");
         },
         toolbox_i18n::tr("lang.list.sub"), toolbox_val("id_ui_lang", "0"))
      .list("id_cheats_shortcut", toolbox_i18n::tr("sc.cheats"),
            [](ps5ui::ListBuilder& L) {
              L.item("id_cheats_shortcut_0", toolbox_i18n::tr("sc.off"), "0")
                  .item("id_cheats_shortcut_1", toolbox_i18n::tr("sc.r3_l3"),
                        "1")
                  .item("id_cheats_shortcut_2", toolbox_i18n::tr("sc.l2_tri"),
                        "2")
                  .item("id_cheats_shortcut_3",
                        toolbox_i18n::tr("sc.long_options"), "3")
                  .item("id_cheats_shortcut_4",
                        toolbox_i18n::tr("sc.long_share"), "4")
                  .item("id_cheats_shortcut_5", toolbox_i18n::tr("sc.share"),
                        "5");
            },
            toolbox_i18n::tr("sc.cheats.sub"), toolbox_val("id_cheats_shortcut"))
      .list("id_toolbox_shortcut", toolbox_i18n::tr("sc.toolbox"),
            [](ps5ui::ListBuilder& L) {
              L.item("id_toolbox_shortcut_0", toolbox_i18n::tr("sc.off"), "0")
                  .item("id_toolbox_shortcut_1", toolbox_i18n::tr("sc.l2_r3"),
                        "1")
                  .item("id_toolbox_shortcut_2",
                        toolbox_i18n::tr("sc.long_share"), "2")
                  .item("id_toolbox_shortcut_3", toolbox_i18n::tr("sc.share"),
                        "3");
            },
            toolbox_i18n::tr("sc.toolbox.sub"),
            toolbox_val("id_toolbox_shortcut"));
}

void append_toolbox_debug_group(ps5ui::Group& g) {
  g.list("id_log_level", toolbox_i18n::tr("log.level"),
         [](ps5ui::ListBuilder& L) {
           L.item("id_log_level_off", toolbox_i18n::tr("log.off"), "0")
               .item("id_log_level_error", toolbox_i18n::tr("log.error"), "1")
               .item("id_log_level_warn", toolbox_i18n::tr("log.warn"), "2")
               .item("id_log_level_info", toolbox_i18n::tr("log.info"), "3");
           if constexpr (ONION_LOG_COMPILE_LEVEL >= ONION_LOG_DEBUG) {
             L.item("id_log_level_debug", toolbox_i18n::tr("log.debug"), "4");
           }
           if constexpr (ONION_LOG_COMPILE_LEVEL >= ONION_LOG_TRACE) {
             L.item("id_log_level_trace", toolbox_i18n::tr("log.trace"), "5");
           }
         },
         toolbox_i18n::tr("log.level.sub"), toolbox_val("id_log_level", "3"))
      .toggle("id_app_jailbreak_enabled",
              toolbox_i18n::tr("app_jailbreak.enabled"),
              toolbox_on("id_app_jailbreak_enabled"),
              toolbox_i18n::tr("app_jailbreak.enabled.sub"))
      .toggle("id_debug_jb", toolbox_i18n::tr("debug.jb"),
              toolbox_on("id_debug_jb"), toolbox_i18n::tr("debug.jb.sub"))
      .text_field("id_np_env", toolbox_i18n::tr("debug.np_env"),
                  toolbox_i18n::tr("debug.np_env.sub"), "basic_latin", "1",
                  "16", "/NP/env", toolbox_i18n::tr("debug.np_env.confirm"),
                  toolbox_i18n::tr("debug.np_env.confirm_phrase"));
}

void append_toolbox_about_group(ps5ui::Group& g) {
  /* About page top: same ONIONHEN_VERSION as welcome toast / beta banner. */
  char build_line[160];
  std::snprintf(build_line, sizeof(build_line), toolbox_i18n::tr("about.build"),
                ONIONHEN_VERSION);

  g.label("id_about_build", build_line, ps5ui::Style::Center)
      .group(
       "id_donation_methods", toolbox_i18n::tr("about.donate"),
       [](ps5ui::Group& d) {
         d.label("id_method_info", toolbox_i18n::tr("about.donate.methods"),
                    ps5ui::Style::Center)
             .label("id_method_1",
                    "- Ko-fi  | https://ko-fi.com/0xp0co",
                    ps5ui::Style::Center)
             .label("id_method_2", "- 微信｜polichan01", ps5ui::Style::Center)
             .button("id_author_0xp0co", "麒麟/0xp0co", std::nullopt, "@0xp0co",
                     kIconAuthorAvatar)
             .label("id_author_donor_spacer", "　", ps5ui::Style::Center)
             .label("id_donor_info", "★ 捐赠者 ★", ps5ui::Style::Center)
             .button("id_donator_aglx", "爱过流星", std::nullopt,
                     std::nullopt, kIconDonatorAglx)
             .button("id_donator_ljf", "狂爱龙卷風", std::nullopt,
                     std::nullopt, kIconDonatorLjf)
             .button("id_donator_szx", "石之心", std::nullopt,
                     std::nullopt, kIconDonatorSzx);
       },
       toolbox_i18n::tr("about.donate.sub"), kIconDonations)
      .group(
          "id_onionhen_credits", toolbox_i18n::tr("about.credits"),
          [](ps5ui::Group& c) {
            const std::string ver =
                std::string("★ OnionHEN ") + ONIONHEN_VERSION;
            c.label("id_about_version", ver, ps5ui::Style::Center)
                .label("id_about_lineage", toolbox_i18n::tr("about.lineage"),
                       ps5ui::Style::Center)
                .label("id_about_lineage_1",
                       toolbox_i18n::tr("about.lineage.etahen"),
                       ps5ui::Style::Center)
                .label("id_about_lineage_2",
                       toolbox_i18n::tr("about.lineage.goldhen"),
                       ps5ui::Style::Center)
                .label("id_about_testers", toolbox_i18n::tr("about.testers"),
                       ps5ui::Style::Center)
                .label("id_about_testers_intro",
                       toolbox_i18n::tr("about.testers.intro"),
                       ps5ui::Style::Center)
                .label("id_about_tester_1",
                       "即食面 · 雨之声 · 大饼电玩",
                       ps5ui::Style::Center)
                .label("id_about_tester_2",
                       "安定区 · 随风 · 麒麟",
                       ps5ui::Style::Center)
                .label("id_about_tester_3",
                       "尼克库尔曼 · 云 · 啊烦",
                       ps5ui::Style::Center)
                .label("id_about_tester_4",
                       "小小蔡",
                       ps5ui::Style::Center)
                .label("id_about_more", toolbox_i18n::tr("about.more"),
                       ps5ui::Style::Center);
          },
          toolbox_i18n::tr("about.credits.sub"), kIconThanks)
      .group(
          "id_inc_project", toolbox_i18n::tr("about.projects"),
          [](ps5ui::Group& p) {
            p.label("id_project_info", toolbox_i18n::tr("about.projects.info"),
                    ps5ui::Style::Center)
                .label("id_project_1",
                       "etaHEN — https://github.com/LightningMods/etaHEN",
                       ps5ui::Style::Center)
                .label("id_project_2",
                       "PS5 Payload Dev SDK — "
                       "https://github.com/ps5-payload-dev/sdk",
                       ps5ui::Style::Center)
                .label("id_project_3",
                       "PS5 Payload Dev elfldr — "
                       "https://github.com/ps5-payload-dev/elfldr",
                       ps5ui::Style::Center)
                .label("id_project_4",
                       "libhijacker (astrelsky) — "
                       "https://github.com/astrelsky/libhijacker",
                       ps5ui::Style::Center)
                .label("id_project_5",
                       "NineS (buzzer-re) — "
                       "https://github.com/buzzer-re/NineS",
                       ps5ui::Style::Center)
                .label("id_project_6",
                       "kstuff (sleirsgoevy / EchoStretch)",
                       ps5ui::Style::Center)
                .label("id_project_7",
                       "cJSON — https://github.com/DaveGamble/cJSON",
                       ps5ui::Style::Center)
                .label("id_project_8",
                       "7-Zip LZMA SDK — https://www.7-zip.org/sdk.html",
                       ps5ui::Style::Center)
                .label("id_project_9",
                       "miniz / Keystone (cheats engine)",
                       ps5ui::Style::Center);
          },
          toolbox_i18n::tr("about.projects.sub"), kIconProject);
}

} // namespace

void generate_toolbox_xml(std::string& new_xml) {
  ps5ui::Page page("id_debug_settings", toolbox_i18n::tr("root.title"));
  page.root_focus("id_group_pkg");

#if defined(ONION_ENABLE_BETA_TRIAL)
  /*
   * Beta notice at the top of the root settings list (centered labels).
   * Visible as soon as the toolbox opens — no need to enter a subgroup.
   * Focus still lands on the first interactive group (PKG).
   * Format string is decoded at runtime (see decode_beta_banner_fmt) so the
   * zh/en redistribution notice is not a plain string in the ELF.
   */
  {
    const bool en = toolbox_i18n::active_lang() == toolbox_i18n::Lang::En;
    const unsigned char *enc =
        en ? kBetaBannerEnEnc : kBetaBannerZhEnc;
    const size_t enc_len =
        en ? sizeof(kBetaBannerEnEnc) : sizeof(kBetaBannerZhEnc);
    std::string fmt = decode_beta_banner_fmt(enc, enc_len);
    char beta_banner[192];
    std::snprintf(beta_banner, sizeof(beta_banner), fmt.c_str(),
                  ONIONHEN_VERSION);
    /* Drop decoded format promptly; banner text remains only in the UI buffer. */
    for (char &c : fmt)
      c = '\0';
    page.label("id_beta_banner", beta_banner, ps5ui::Style::Center);
  }
#endif

  page.group(
          "id_group_pkg", toolbox_i18n::tr("group.pkg"),
          [](ps5ui::Group& g) { append_toolbox_pkg_group(g); },
          toolbox_i18n::tr("group.pkg.sub"), kIconPkg,
          "id_game_package_installer")
      .group(
          "id_group_payloads", toolbox_i18n::tr("group.payloads"),
          [](ps5ui::Group& g) { append_toolbox_payloads_group(g); },
          toolbox_i18n::tr("group.payloads.sub"), kIconPlugins, "id_payloads")
      .group(
          "id_group_game", toolbox_i18n::tr("group.game"),
          [](ps5ui::Group& g) { append_toolbox_game_group(g); },
          toolbox_i18n::tr("group.game.sub"), kIconGame, "id_cheats")
      .group(
          "id_group_display", toolbox_i18n::tr("group.display"),
          [](ps5ui::Group& g) { append_toolbox_display_group(g); },
          toolbox_i18n::tr("group.display.sub"), kIconMonitor,
          "id_overlay_opts")
      .group(
          "id_group_connection", toolbox_i18n::tr("group.connection"),
          [](ps5ui::Group& g) { append_toolbox_connection_group(g); },
          toolbox_i18n::tr("group.connection.sub"), kIconAccount,
          "remote_play")
      .group(
          "id_group_system", toolbox_i18n::tr("group.system"),
          [](ps5ui::Group& g) { append_toolbox_system_group(g); },
          toolbox_i18n::tr("group.system.sub"), kIconSettings,
          "id_group_fan")
      .group(
          "id_group_preferences", toolbox_i18n::tr("group.preferences"),
          [](ps5ui::Group& g) { append_toolbox_preferences_group(g); },
          toolbox_i18n::tr("group.preferences.sub"), kIconShortcuts,
          "id_ui_lang")
      .group(
          "id_group_debug", toolbox_i18n::tr("group.debug"),
          [](ps5ui::Group& g) { append_toolbox_debug_group(g); },
          toolbox_i18n::tr("group.debug.sub"), kIconDebug,
          "id_app_jailbreak_enabled")
      .group(
          "id_onionhen_credit_options", toolbox_i18n::tr("group.about"),
          [](ps5ui::Group& g) { append_toolbox_about_group(g); },
          toolbox_i18n::tr("group.about.sub"), kIconAbout, std::nullopt,
          ps5ui::Style::Center);

  new_xml = page.build();
}
