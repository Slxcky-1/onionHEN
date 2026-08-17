# Shared translations

User-facing text lives in one JSON file per locale and is compiled into the
ELF. The console does not load locale files at runtime.

Current locales: `zh-CN.json` (简体中文) and `en-US.json` (English).
A third language is not wired yet — adding one still requires C++ and
CMake changes, not just a new JSON file.

## File layout

Each locale file has exactly two sections:

| Section | Keys | Runtime |
|---|---|---|
| `toolbox` | dotted ids such as `fan.enable` | `toolbox_i18n::tr("fan.enable")` |
| `notifications` | `notify.*` ids such as `notify.fan.open_failed` | `onion_notify(true, "notify.fan.open_failed")` |

`en-US.json` values are the English sentences. `zh-CN.json` values are the
Chinese sentences. Both files must contain the same keys.

The build runs `generate_catalog.py` and rejects:

- missing keys on either side
- empty keys or embedded NUL
- mismatched `printf` conversions (`%s`, `%d`, `%i`, `%u`, `%X`, …)

Generated tables are embedded in `shellui` (toolbox) and
`libonion_platform` (notifications).

## How to add a Toolbox string

1. Add the same key to **both** JSON files under `toolbox`.
2. Prefer one complete sentence with `printf` placeholders when values are
   inserted. Do not split a sentence across several keys and concatenate
   them in C++ (word order cannot be translated).

   ```json
   "cheats.enable_fmt": "Enable/disable %s for %s"
   ```

   ```json
   "cheats.enable_fmt": "为 %s 启用/禁用 %s"
   ```

3. Format with the shared helper, not a local `snprintf` wrapper:

   ```cpp
   toolbox_i18n::format("cheats.enable_fmt", game, cheat);
   ```
4. Rebuild. Missing the key in one locale fails the catalog step.
5. Switch Toolbox language, leave the page, and reopen it. XML is built
   when the page opens.

Missing toolbox keys render as the key itself (visible in the menu).

## How to add a notification

1. Add a stable `notify.<area>.<name>` key to **both** JSON files under
   `notifications`. Never use the English sentence as the key.
2. Keep `printf` placeholders identical in both languages.
3. Pass the key, not the English text:

   ```c
   onion_notify(true, "notify.fan.open_failed");
   onion_notify(true, "notify.payload.loading", name);
   onion_notify_debug("notify.trial.days", days);
   onion_notify_rich("notify.brand", "notify.boot.starting", ...);
   ```

4. Do not pass a raw English format string. Unknown keys are shown
   unchanged (English-looking leftover, or a debug `"%s"` passthrough).

ShellUI's `notify("…")` helper forwards to the same catalog. Unpacker
has its own `notify()` and does **not** use this catalog.

## How to add a language (not just a JSON file)

Today the generator and runtime are still two-locale (`zh` / `en`):

- `generate_catalog.py` is invoked with `--zh-cn` and `--en-us`
- tables are `{key, zh, en}`
- settings values are `system` / `zh-Hans` / `en` (`0` / `1` / `2`)
- Toolbox language list is three hardcoded items

To add another language later you must, at minimum:

1. Add `<locale>.json` with the same keys as `en-US.json`
2. Extend the generator, CMake `DEPENDS`, and both catalog table shapes
3. Extend language parse/serialize and the Toolbox language list
4. Decide how PS5 system-language ids map onto the new locale
   (ids `10` and `11` currently become `zh-Hans`; everything else is `en`)

Until that lands, do not add `ja-JP.json` or similar — it will be ignored.

## Intentional exclusions

These user-visible strings are **not** in the JSON catalogs on purpose:

| Surface | Why |
|---|---|
| Settings menu label `★OnionHEN Tools` | Equal-length binary patch of `★Debug Settings`. Length is fixed. |
| HomeUI top-nav `OnionHEN` | Brand token, same in every language. |
| Notification watermark `[OnionHEN]` | Brand prefix in `onion_notify_format`. |
| About names, handles, Ko-fi URL, project URLs | Proper nouns / addresses. |
| Beta trial banner and integrity/redistrib toasts | XOR-obfuscated via `encrypt_banner.py`, already bilingual. |
| Unpacker start-failure toasts | First-stage loader; no language setting and its own `notify()`. English only until it is wired to `onion_notify`. |
| Cheat names / descriptions from cheat files | Come from the cheat JSON, not OnionHEN. |

Logs (`LOG_*`) are developer-facing and stay English.

## Current zh / en coverage

Checked against call sites (not just the JSON files):

- Toolbox menus, game-options cheat entry, PKG `GetString` hooks, and
  About donor/WeChat labels go through `toolbox_i18n::tr()`.
- Daemon / util / shellui / bootstrapper / trial toasts go through
  `notify.*` keys. Host tests cover zh and en lookup.
- Four toolbox keys are unused leftovers from an older menu grouping:
  `group.lang`, `group.lang.sub`, `group.shortcuts`, `group.shortcuts.sub`.
  They are translated but not shown.
- Welcome toast still concatenates `version + notify.boot.made_by + author`.
  That word order is correct for current zh and en only.

So for the two shipped locales, user-facing ShellUI / daemon / util text
is localized, except the intentional exclusions above.
