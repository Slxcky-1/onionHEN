#pragma once

#include "cheats/sync/i_git_mirror.hpp"
#include "cheats/sync/types.hpp"

#include <memory>

namespace onion::cheats::sync {

struct GitMirrorPick {
  std::unique_ptr<IGitMirror> primary;
  std::unique_ptr<IGitMirror> fallback;
};

class GitMirrorFactory {
public:
  /**
   * @param pref        settings / IPC override
   * @param ui_lang     onion::Settings::ui_lang
   * @param system_lang SCE_SYSTEM_SERVICE_PARAM_ID_LANG (only used when ui is system)
   */
  static GitMirrorPick create(CheatMirrorPref pref, int ui_lang,
                              int system_lang);

  static std::unique_ptr<IGitMirror> make(CheatMirrorId id);

  static CheatMirrorPref parsePref(const char *token, CheatMirrorPref def);
  static const char *prefName(CheatMirrorPref pref);
};

} // namespace onion::cheats::sync
