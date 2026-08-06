# HomeUI / Settings compatibility handoff

本文是给后续 AI/维护者的适配流程文档。目标是把新固件的
`NPXS40002` HomeUI 顶部导航入口和 `NPXS40008` Settings DebugSettings
入口接入 OnionHEN，同时保持当前的 profile/strategy 表结构。

适配时不要把固件判断散落在 hook 里。新增固件应该尽量只扩展兼容表、
字节策略和测试。

## 输入文件

每个固件 dump 至少需要两个已解密 RNPS 文件：

| App | 作用 | 典型文件 |
|-----|------|----------|
| `NPXS40002` | HomeUI 顶部导航入口 | `NPXS40002.bin` |
| `NPXS40008` | Settings / DebugSettings bundle | `NPXS40008.bin` |

推荐目录形态：

```text
/path/to/12.02DUMP/
  NPXS40002.bin
  NPXS40008.bin
```

如果文件来自 PS5 机器，先用带解密功能的 FTP / ftpsrv 导出解密后的
`NPXS*.bin`。不要把未解密文件直接拿来做 profile。

## 已知复用

- `4.03`、`4.50`、`4.51` 的 `NPXS40002.bin` 整文件完全一致（SHA-256
  `6db944372cfe8b7d50328ed4bd47c8cae6917821fb30f20004e7f85c673fe00a`）。
  它们使用旧 RNPS JavaScript bundle，不是 Hermes HBC，共用一个 legacy
  HomeUI profile。
- `9.00` 的 `NPXS40002` 使用 Hermes v89，`hbc_file_length=0x1846e0`、
  `source_hash=587635687e0a190e38425232c39092888da5adbe`，使用独立 HomeUI
  profile。
- `10.2DUMP` 的 `NPXS40002`、`NPXS40008` 分别与 10.4 对应文件整文件
  完全一致，直接复用 `10.4/10.6` HomeUI profile 和 10.4 Settings
  fingerprint；Settings route 为 standard。
- `11.2DUMP` 的 `NPXS40002` 与 11.4/11.6 完全一致；`NPXS40008`
  使用独立 old-route 指纹：`hbc_file_length=0x4f45b8`、
  `source_hash=d03462a912c4b5b8db4a98d044b9d488a2dffc7a`。
- `11.40DUMP` 的 `NPXS40002` HBC 与 11.6 完全一致，复用
  `11.4/11.6` HomeUI profile。`NPXS40008` 使用独立 Settings 指纹：
  `hbc_file_length=0x4f45c4`、
  `source_hash=a7b731571f84b6cdaf7c4227a980ba5ee20004a8`，route 为 old。
- `NPXS40009` 不包含 `debug_settings` / `debug_settings_old`，不能作为
  Settings profile 指纹来源。
- `12.4DUMP` 的 `NPXS40002` 与 12.7、`NPXS40008` 与 12.20 分别整文件
  完全一致，复用对应 HomeUI profile 和 Settings fingerprint；route 为 old。
- `12.6DUMP` 的 `NPXS40002` 与 12.7 完全一致；`NPXS40008` 使用独立
  old-route 指纹：`hbc_file_length=0x4e9028`、
  `source_hash=75747bb5fa7e3a4e22d557882f5281e4d1f12959`。

## 第一轮识别

先用仓库脚本识别 RNPS payload 并提取指纹和关键字符串：

```sh
python3 scripts/analyze_rnps_dump.py /path/to/DUMP --allow-unsupported
```

Hermes bundle 会输出：

- RNPS 内 HBC offset
- HBC version
- `hbc_file_length`
- `source_hash`
- 已知 profile 是否命中
- 关键字符串 offset
- Settings route 推断：`standard` / `old`

4.x legacy HomeUI 会改为输出：payload offset、旧 bundle magic、payload
size、整文件 SHA-256、匹配的 legacy profile 和关键源码字符串 offset。

如果只想给其他工具消费：

```sh
python3 scripts/analyze_rnps_dump.py /path/to/DUMP --json --allow-unsupported
```

判断分支：

- `NPXS40002` 已命中 HomeUI profile：通常不用新增导航 profile。
- `NPXS40002` 未命中：需要新增 HomeUI profile 和 byte set。
- `NPXS40008` 未命中：需要新增 Settings fingerprint。
- Settings route 推断必须和 profile route 一致。

12.0/12.02 适配记录：两个版本的 `NPXS40002` dump 与现有 12.20
HomeUI profile 完全一致，直接复用；`NPXS40008` 也使用同一份 bundle
指纹和 old route：
`hbc_file_length=0x4e7bec`、
`source_hash=fc7c4f15af42929e1d52420c2d174944b4a88043`。

## Settings 兼容

相关文件：

- `source/include/onion/debug_settings_route_policy.hpp`
- `source/util/tests/test_debug_settings_route_policy.cpp`
- `scripts/analyze_rnps_dump.py`

当前规则：

| 固件范围 | route | URI |
|----------|-------|-----|
| `10.x` 到 `10.6` | `standard` | `function=debug_settings` |
| `11.x` 及以上 | `old` | `function=debug_settings_old` |

新增 Settings 兼容时，不只看 `hbc_file_length`。必须同时匹配：

- `hbc_file_length`
- HBC `source_hash`

这样能避免误匹配其他 Settings bundle。

添加步骤：

1. 从 `analyze_rnps_dump.py` 输出复制 `hbc_file_length` 和 `source_hash`。
2. 按 route 放入：
   - `kStandardSettingsBundles`
   - `kOldRouteSettingsBundles`
3. 在 `scripts/analyze_rnps_dump.py` 的 `KNOWN_SETTINGS_PROFILES` 加同一条。
4. 在 host test 中新增 bundle hash 测试。
5. 如版本边界变了，调整 `kCompatibilityProfiles` 的 `{min, max, route}` 范围，
   并新增版本路由测试。

注意：`debug_settings_old` 包含 `debug_settings` 子串，所以脚本里看到
`debug_settings` count 不为 0 并不代表 route 是 standard。以
`debug_settings_old` 是否存在为优先判断。

## HomeUI 兼容

相关文件：

- `source/shellui/src/homeui_top_nav_patch.cpp`
- `source/shellui/src/homeui_top_nav_profiles.inc`
- `scripts/analyze_rnps_dump.py`
- `scripts/verify_homeui_top_nav_fixes.py`

HomeUI 有两类 bundle：

- 9.00 及当前已知的新固件使用 Hermes HBC，由固件 profile 描述 offset
  和字节。
- 4.03/4.50/4.51 使用旧 RNPS JavaScript bundle，由 legacy 分支执行等长
  源码替换；不能把它当作 HBC profile。

Hermes 兼容由两层组成：

- `HomeUiPatchProfile`：固件 HBC 指纹和所有 patch offset。
- `HomeUiPatchBytes`：该固件对应的字节替换策略。

新增固件不要直接在 patch 流程里写 `if (version == ...)`，而是新增
`HomeUiPatchBytes` 和表项。

安全的导航结构统一为：

```text
[Search, ApplicationErrorEventTrigger, Settings, Profile]
```

OnionHEN 使用原本 77 字节的 `ApplicationErrorEventTrigger` 按钮函数作为
宿主，`Fps` 保持原实现。不要再劫持 `Fps` 函数体；它在游戏退出、HomeUI
重新挂载时会参与恢复流程，旧方案曾引发 RN JS executor 崩溃。

### 旧 RNPS JavaScript bundle（4.x）

旧 bundle magic 为 `e5 d1 0b fb`，位于 RNPS payload offset（当前三个
dump 都是 `0xb20`）。兼容采用三处等长替换：

```text
["Fps","Search","Settings","Profile"]
→ ["Search","App","Settings","Profile"]

t.Fps=P
→ t.App=h
```

同时把 AppError 的 383 字节源码块改成
`useInteractivePress({link:"OnionHEN?NavUI=1"})` 按钮，设置
`/system_ex/vsh_asset/onionhen.png`、空标题，并用空格填满剩余字节。
旧 bundle 没有 Hermes footer SHA-1，不能调用 HBC footer 更新逻辑。

### 1. 提取 HBC

分析脚本会告诉你 HBC offset。需要反汇编时可临时抽出 HBC：

```sh
python3 - <<'PY'
from pathlib import Path

src = Path("/path/to/DUMP/NPXS40002.bin")
out = Path(".tmp/hbc/new_fw.hbc")
magic = bytes([0xc6, 0x1f, 0xbc, 0x03, 0xc1, 0x03, 0x19, 0x1f])
data = src.read_bytes()
off = data.find(magic)
if off < 0:
    raise SystemExit("HBC magic not found")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(data[off:])
print(f"HBC offset=0x{off:x} out={out}")
PY
```

反汇编：

```sh
/Users/chenpy/.local/bin/hbc-disassembler .tmp/hbc/new_fw.hbc .tmp/hbc/new_fw.dis
```

`.tmp/` 是临时分析目录，不要提交。

### 2. 填 HomeUiPatchProfile

从分析脚本和反汇编结果填：

| 字段 | 来源 |
|------|------|
| `hbc_version` | HBC header offset `0x08` |
| `file_length` | HBC header offset `0x20` |
| `source_hash` | HBC header offset `0x0c`，20 bytes |
| `title_id` | 字符串 `NPXS40002` offset |
| `app_error_event_trigger` | 字符串 `ApplicationErrorEventTrigger` offset |
| `navigate_to_home` | 字符串 `pshomeui:navigateToHome` 第一个 offset |
| `download_error_string` | 字符串 `download_error` offset |
| `custom_icon_uri` | 字符串 `homeui ApplicationErrorEvent test` offset |
| `top_nav_link_uri` | 字符串 `Trigger AppError` offset |

剩余字段需要二进制/反汇编定位：

| 字段 | 定位方式 |
|------|----------|
| `home_icon_order` | 找 top nav array bytes，通常旧顺序是 `Fps, Search, Settings, Profile` |
| `fps_factory` | top-nav module 中 Fps factory / export 相关字节 |
| `custom_icon_value` | AppError object buffer 里 `download_error` string id 的 2-byte 值 |
| `custom_title_value` | AppError object buffer 里 `Trigger AppError` string id 的 2-byte 值 |
| `fps_body` | Fps function bytecode 起始 offset |
| `app_error_body` | `ApplicationErrorEventTrigger` 的 77-byte function 起始 offset |

### 3. 导出字符串 ID

在反汇编里找 top nav module 和相关函数：

```sh
rg -n -C 8 "ApplicationErrorEventTrigger|PutById.*Fps|Function #|useInteractivePress|Object: \\{'iconId'" .tmp/hbc/new_fw.dis
```

常用 string id：

- `ApplicationErrorEventTrigger`
- `Fps`
- `Search`
- `Settings`
- `Profile`
- `homeui ApplicationErrorEvent test`
- `Trigger AppError`
- `useInteractivePress`
- `link`
- `jsx`
- `default`
- `onPress`

这些 ID 决定 icon order、对象表替换值和 Fps body replacement。

### 4. 定位 home icon order

目标是把 `ApplicationErrorEventTrigger` 放到 `Search` 后面作为 OnionHEN
入口，并保留 `Fps` 原实现。

旧形态一般是：

```text
[Fps, Search, Settings, Profile]
```

新形态：

```text
[Search, ApplicationErrorEventTrigger, Settings, Profile]
```

用 string id 组成 HBC 里的 `NewArrayWithBuffer` 字节序列搜索。例如某固件：

```text
54 <Fps id le16> <Search id le16> <Settings id le16> <Profile id le16>
```

搜索命令示例：

```sh
python3 - <<'PY'
from pathlib import Path
h = Path(".tmp/hbc/new_fw.hbc").read_bytes()
patterns = {
    "old": bytes.fromhex("54 e3 1b 4d 1c 4e 15 85 16"),
    "new": bytes.fromhex("54 4d 1c 3e 16 4e 15 85 16"),
}
for name, pat in patterns.items():
    print(name, hex(h.find(pat)))
PY
```

只在唯一 offset 命中时写入 profile。

### 5. 定位 AppError object buffer

在反汇编里找：

```text
Object: {'iconId': 'download_error', 'onPress': null, 'title': 'Trigger AppError'}
```

然后在 HBC 原始字节里找对象表附近的 string id：

```text
... 51 <download_error id le16> 01 51 <Trigger AppError id le16> ...
```

`custom_icon_value` 指向 `<download_error id le16>`，replacement 指向
`homeui ApplicationErrorEvent test` 的 string id。之后同一字符串内容会被
替换为 `/system_ex/vsh_asset/onionhen.png`。

`custom_title_value` 指向 `<Trigger AppError id le16>`，replacement 为
`ff 00`，用于显示空标题。

不要只靠肉眼偏移。必须用 Python 检查当前位置的 old bytes 是否符合预期。

### 6. 写 AppError body replacement

使用 `ApplicationErrorEventTrigger` 的完整 77 字节函数作为按钮宿主：

- `useInteractivePress({ link: "OnionHEN?NavUI=1" })`
- `jsx(default, { iconId, onPress, title })`

需要根据新固件 string id 修改 body bytes 中这些位置：

- `useInteractivePress`
- `Trigger AppError` 字符串 ID，后续该字符串内容被替换为
  `OnionHEN?NavUI=1`
- `link`
- `jsx`
- `default`
- object buffer pair
- `onPress`

replacement 必须和 stock AppError body 等长，目前各 profile 都是 77 bytes。
固定数组长度会由编译器校验。`old_fps_body_prefix` 只用于修复历史版本残留的
Fps-body 劫持；新 profile 不能把 OnionHEN body 写到 `fps_body`。

### 7. 临时 patch 并反汇编验证

写进源码前，先对 `.tmp/hbc/new_fw.hbc` 做临时 patch，更新 HBC footer SHA1，
再反汇编。

验证至少包含：

```sh
rg -n "Array: \\['Search', 'ApplicationErrorEventTrigger', 'Settings', 'Profile'\\]" .tmp/hbc/new_fw_patched.dis
rg -n "Object: \\{'iconId': '/system_ex/vsh_asset/onionhen.png', 'onPress': null, 'title': ''\\}" .tmp/hbc/new_fw_patched.dis
rg -n "String: 'OnionHEN\\?NavUI=1'" .tmp/hbc/new_fw_patched.dis
```

如果反汇编失败，或者对象/路由没有按预期出现，不要把该 byte set 写进源码。

## 脚本同步

每次新增兼容 profile，都同步 `scripts/analyze_rnps_dump.py`：

- `KNOWN_HOMEUI_PROFILES`
- `KNOWN_LEGACY_HOMEUI_PROFILES`
- `KNOWN_SETTINGS_PROFILES`

这个脚本既是提取工具，也是回归验证工具。只有 HomeUI 在本次适配范围内时，
可带 `--allow-unsupported` 保留未支持 Settings 的诊断；两个 App 都完成后，
脚本应能在不带该参数时返回 PASS。

## 测试矩阵

最低验证清单：

```sh
python3 scripts/analyze_rnps_dump.py /path/to/new/DUMP --allow-unsupported
python3 scripts/analyze_rnps_dump.py /path/to/known/10.01DUMP
python3 scripts/analyze_rnps_dump.py /path/to/known/10.6DUMP
python3 scripts/analyze_rnps_dump.py /path/to/known/11.6DUMP
python3 scripts/analyze_rnps_dump.py /path/to/known/12.7DUMP
python3 scripts/verify_homeui_top_nav_fixes.py

python3 -m py_compile scripts/analyze_rnps_dump.py
git diff --check
make -C source/util/tests test
cmake --build build --target shellui -j 8
cmake --build build --target daemon -j 8
```

如果新增了 HomeUI byte set，还要保留临时 patch 的反汇编检查记录在交接说明
或 PR/commit 描述里。

## 常见坑

- `debug_settings_old` 包含 `debug_settings` 子串，route 推断要优先看 old。
- `NPXS40002` 的 file size 和 HBC `file_length` 不是同一个字段，profile 用
  HBC `file_length`。
- HomeUI string offset 是 HBC 内 offset，不是 RNPS 文件内 offset。
- 4.x legacy offset 是相对旧 JavaScript payload 起点，不是 RNPS 文件起点；
  旧 bundle 不更新 Hermes footer SHA-1。
- `custom_icon_value` / `custom_title_value` 是 object buffer 中的 2-byte
  string id 位置，不是字符串内容 offset。
- 替换字符串必须等长：
  - `homeui ApplicationErrorEvent test`
  - `/system_ex/vsh_asset/onionhen.png`
  - `Trigger AppError`
  - `OnionHEN?NavUI=1`
- Runtime patch 会更新 HBC footer SHA1；临时 HBC 验证也要更新 footer。
- 顶部导航按钮使用 `ApplicationErrorEventTrigger` 作为宿主；不要重新劫持
  `Fps` 函数体。
- `.tmp/hbc/*`、反汇编输出、candidate HBC 都是分析产物，不提交。

## 交接模板

给下一个 AI 的最小交接信息：

```text
固件版本：
Dump 目录：

NPXS40002:
- hbc_version:
- hbc_file_length:
- source_hash:
- 是否复用已知 HomeUI profile:
- 如新增 profile，列出 offsets:

NPXS40008:
- hbc_file_length:
- source_hash:
- route: standard / old

HomeUI 临时 patch 反汇编检查:
- nav order:
- icon object:
- OnionHEN?NavUI=1:

已运行验证:
- analyze_rnps_dump.py new dump:
- known dump regressions:
- host tests:
- shellui build:
- daemon build:
```
