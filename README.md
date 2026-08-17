<p align="center">
  <img src="assets/logo.png" alt="OnionHEN" height="128" width="128"/>
</p>

<p align="center">
  <b>OnionHEN</b><br/>
  An all-in-one homebrew enabler and Toolbox for PlayStation 5
</p>

<p align="center">
  <a href="README_ZH.md">简体中文</a>
  ·
  <b>English</b>
</p>

<p align="center">
  <b><a href="#features">Features</a></b>
  ·
  <b><a href="#run">Run</a></b>
  ·
  <b><a href="#build">Build</a></b>
  ·
  <b><a href="#configuration">Configuration</a></b>
  ·
  <b><a href="#support">Support</a></b>
  ·
  <b><a href="#credits">Credits</a></b>
</p>

---

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="license"/></a>
  <img src="https://img.shields.io/badge/Platform-PlayStation%205-003791?style=flat&logo=playstation" alt="PlayStation 5"/>
  <img src="https://img.shields.io/badge/C-00599C?style=flat&logo=c&logoColor=white" alt="C"/>
  <img src="https://img.shields.io/badge/C++-00599C?style=flat&logo=cplusplus&logoColor=white" alt="C++"/>
  <img src="https://img.shields.io/badge/Build-CMake-064F8C?style=flat&logo=cmake" alt="CMake"/>
</p>

<p align="center">
  OnionHEN is a modular PS5 payload stack that combines system preparation,
  a ShellUI Toolbox,<br/>payload management, game overlays, cheats, and runtime
  services behind one entry payload.
</p>

<p align="center">
  <img src="assets/screenshot_01.png" alt="OnionHEN home-screen entry"/>
</p>

<p align="center">
  <img src="assets/screenshot_02.png" alt="OnionHEN in-game monitor bar"/>
</p>

<p align="center">
  <img src="assets/screenshot_03.png" alt="OnionHEN Toolbox"/>
</p>

<br>

# Features

OnionHEN focuses on a practical, maintainable homebrew environment for exploited PS5 systems.

- **ShellUI Toolbox** — an integrated settings interface injected into the PS5 ShellUI
- **System preparation** — privilege setup, filesystem remounting, and update-partition blocking
- **fSELF / fPKG support** — optional kernel functionality provided by the embedded `kstuff` payload
- **Payload manager** — launch and stop bare `.elf` payloads, with optional automatic startup
- **Game overlay** — configurable FPS, CPU, GPU, RAM, temperature, usage, and network information
- **Cheat engine** — local JSON, SHN, MC4, and ShnExt files with runtime toggle support
- **Console tools** — Rest Mode controls, Remote Play options, external HDD tools, title IDs, fan settings, shortcuts, and game options
- **App jailbreak** — whitelist homebrew can request privilege escape via daemon sandbox FIFO
- **Resilient runtime** — critical and utility daemons are separated; the main daemon can restart the utility daemon
- **Centralized configuration** — Toolbox and daemon settings share one versioned `config.ini` schema

OnionHEN intentionally does not bundle a kernel exploit. It still needs an
external `elfldr` on **9021** for the initial bootstrap, then starts its own
private embedded loader on **9020** for runtime ELF and user payload launches.

<br>

# Run

### Requirements

1. A PS5 running a compatible kernel exploit
2. An external [PS5 `elfldr`](https://github.com/ps5-payload-dev/elfldr) listening on port **9021** for the initial bootstrap
3. A firmware-compatible `kstuff` build when fSELF / fPKG functionality is required

> [!IMPORTANT]
> Firmware and exploit compatibility are determined by the exploit chain, PS5 payload SDK,
> and `kstuff` version. Do not assume that a payload built for one firmware is safe on another.

### Load OnionHEN

1. Run the kernel exploit and start the external `elfldr` service.
2. Send `OnionHEN.elf` through the loader provided by your exploit host.
3. Wait for the utility daemon, `kstuff`, and main daemon to start in sequence.
4. Open the PS5 settings area to access the OnionHEN Toolbox.

The runtime launch order is deliberately serialized. After bootstrap, OnionHEN
prefers its embedded `onion_elfldr.elf` on **9020** and keeps **9021** as a
compatibility fallback.

```text
OnionHEN.elf → bootstrapper → onion_elfldr.elf (:9020) → util.elf → kstuff.elf → daemon.elf → Toolbox
```

### Payloads

Place standalone payloads in:

```text
/data/OnionHEN/payloads/
```

Only bare `.elf` payloads are supported. Automatic startup can be enabled from the Toolbox;
OnionHEN records that choice with a sibling `.auto_start` marker.

### Cheats

Place cheat files in the flat cheat directory using the title ID and game version:

```text
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>.json
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>.shn
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>.mc4
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>.ShnExt
```

Cheat files are loaded locally. Changes are detected and reloaded without restarting the full stack.

<br>

# Build

### Dependencies

| Dependency | Purpose |
| --- | --- |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) | Prospero compiler, target headers, runtime, and CMake wrapper |
| CMake 3.20+ and Ninja | Configure and build the payload graph |
| Clang / LLVM | Compile the `x86_64-sie-ps5` targets |
| `lzma` or `xz` | Compress the bootstrapper |
| Git and `curl` or `wget` | Initialize submodules and fetch external payload inputs |

### Full build

```shell
git submodule update --init --recursive

export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
./scripts/build.sh --jobs 8
```

The build script configures the project, synchronizes external dependencies, and builds the complete
embedding chain in the required order.

### Common options

| Option | Description |
| --- | --- |
| `--fw <hex>` | Set `PS5_FW_VERSION` / `V_FW` |
| `--build-type Debug\|Release` | Select the CMake build type |
| `--build-dir <path>` | Override the default `build/` directory |
| `--cache-dir <path>` | Override `.cache/dependencies/` |
| `--stub-missing` | Use compile-only placeholder external ELFs; never use on hardware |
| `--skip-unpacker` | Stop after building the bootstrapper |
| `--init-submodules` | Initialize submodules before dependency sync |

Run `./scripts/build.sh --help` for the complete option list.

### Outputs

| Path | Description |
| --- | --- |
| `build/bin/OnionHEN.elf` | Final payload sent to the console |
| `build/bin/bootstrapper.elf` | Uncompressed bootstrapper |
| `build/bin/bootstrapper.elf.lzma` | Bootstrapper embedded by the final payload |
| `build/bin/daemon.elf` | Critical daemon |
| `build/bin/util.elf` | Utility daemon |
| `build/bin/shellui.elf` | Toolbox injection payload |
| `build/lib/*.a` | First-party static libraries |

All generated files stay under `build/`; downloaded inputs are cached under `.cache/dependencies/`.
The `source/` tree is not used as an artifact directory.

### Tests

Host tests cover settings, IPC framing, payload helpers, cheat parsers, relocation, Toolbox routing,
and shared platform code:

```shell
make -C source/util/tests clean
make -C source/util/tests test -j8
```

The host test link expects Keystone to be available through `KEYSTONE_PREFIX`
(default: `/opt/homebrew`).

<br>

# Configuration

OnionHEN creates and shares the same schema through these runtime views:

```text
/data/OnionHEN/config.ini
/user/data/OnionHEN/config.ini
```

Most settings can be changed directly from the Toolbox. The semantic schema
starts at `schema_version=1`. When no config exists, OnionHEN writes an
annotated default based on [`config.ini.example`](config.ini.example).

| Key | Default | Values |
| --- | --- | --- |
| `meta.schema_version` | `1` | `1` |
| `toolbox.language` | `system` | `system`, `zh-Hans`, `zh-Hant`, `en`, `ja`, `ko`, `fr`, `de`, `es`, `pt-BR`, `ar` |
| `startup.open_after_load` | `none` | `none`, `home_menu` |
| `home_screen.show_title_ids` | `false` | `true`, `false` |
| `game_menu.show_onionhen_options` | `true` | `true`, `false` |
| `rest_mode.resume_reinject_delay_seconds` | `0` | seconds |
| `rest_mode.stop_utility_daemon_on_entry` | `false` | `true`, `false` |
| `rest_mode.close_running_game_on_entry` | `false` | `true`, `false` |
| `cheats.memory_backend` | `default` | `default`, `libhijacker` |
| `app_jailbreak.debug_notifications` | `false` | `true`, `false` |
| `cooling.fan_control` | `automatic` | `automatic`, `temperature_threshold` |
| `cooling.temperature_threshold_celsius` | `77` | `0` through `100` |
| `overlay.enabled` | `true` | `true`, `false` |
| `overlay.background` | `true` | `true`, `false` |
| `overlay.edge` | `top` | `top`, `bottom` |
| `overlay.show_cpu` / `overlay.show_gpu` / `overlay.show_memory` | `true` | `true`, `false` |
| `overlay.cpu_usage_mode` | `average` | `average`, `per_core` |
| `overlay.show_ip_address` | `false` | `true`, `false` |
| `shortcuts.cheats_menu` | `off` | `off`, `r3_l3`, `l2_triangle`, `long_options`, `long_share`, `share` |
| `shortcuts.toolbox` | `off` | `off`, `l2_r3`, `long_share`, `share` |

### Runtime data

| Path | Purpose |
| --- | --- |
| `/data/OnionHEN/payloads/` | User payload ELFs |
| `/data/OnionHEN/cheats/` | Flat cheat files |
| `/data/OnionHEN/kstuff.elf` | Optional runtime override for embedded `kstuff` |
| `/data/OnionHEN/OnionHEN.log` | Main runtime log |
| `/data/OnionHEN/OnionHEN_util_daemon.log` | Utility daemon log |

<br>

# Host tools

| Tool | Purpose |
| --- | --- |
| [`scripts/daemon_log.py`](scripts/daemon_log.py) | Stream daemon logs from the console |
| [`scripts/shutdown_onion.py`](scripts/shutdown_onion.py) | Shut down the OnionHEN userland stack without killing `kstuff` |
| [`scripts/ps5_cmake.sh`](scripts/ps5_cmake.sh) | Run CMake through the PS5 payload SDK |
| [`scripts/sync_dependencies.sh`](scripts/sync_dependencies.sh) | Fetch or build external payload inputs |

<br>

# Repository layout

```text
.
├── assets/                    Project artwork
├── docs/                      Architecture and technical notes
├── scripts/                   Build, dependency, and host-side helpers
├── source/                    First-party PS5 source tree
│   ├── bootstrapper/          Startup and embedded payload chain
│   ├── daemon/                Critical daemon and Toolbox injection
│   ├── util/                  Utility daemon, IPC, and cheat engine
│   ├── shellui/               Toolbox and ShellUI hooks
│   ├── unpacker/              Final OnionHEN payload wrapper
│   ├── libonion_*/            Shared first-party libraries
│   ├── common/                Shared low-level implementations
│   └── platform/ps5/stubs/    PS5 system-library link stubs
├── third_party/               External source, archives, and submodules
├── tools/                     Repository-side generators
├── build/                     Generated build outputs (ignored)
└── .cache/dependencies/       Downloaded external inputs (ignored)
```

See [the architecture document](docs/arch.md) for the complete module and IPC map.

<br>

# Troubleshooting

1. Confirm the external bootstrap `elfldr` service is reachable on port **9021** before loading OnionHEN.
2. Confirm the payload was built for the intended firmware and with a complete PS5 payload SDK.
3. If the Toolbox does not appear, wait for the serialized `util → kstuff → daemon` launch chain and inspect the logs.
4. If a full build cannot obtain `kstuff.elf`, retry `./scripts/sync_dependencies.sh` or initialize the submodule.
5. When reporting a problem, include firmware, exploit host, SDK version, build type, logs, and reproducible steps.

<br>

# Contributing

- Keep changes focused and follow the existing snake_case naming convention.
- Preserve third-party file names when syncing upstream code.
- Run the full PS5 build and host tests before submitting a pull request.
- Update `docs/arch.md` when changing module responsibilities, IPC, runtime paths, or dependencies.

<br>

# Support

<a href="https://ko-fi.com/0xp0co"><img src="https://img.shields.io/badge/Ko--fi-Support-FF6433?style=for-the-badge&logo=kofi&logoColor=white" alt="Support OnionHEN on Ko-fi"/></a>

If you find this project helpful, please consider supporting its continued development. Your support is greatly appreciated!

<br>

# Credits

OnionHEN is possible because of the PS5 homebrew and reverse-engineering community.

- [etaHEN](https://github.com/LightningMods/etaHEN) — LightningMods and contributors; original open-source foundation
- [GoldHEN](https://github.com/GoldHEN/GoldHEN) — SiSTR0 and contributors; inspiration for a polished all-in-one HEN
- [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) and the PS5 payload development community
- [kstuff-lite](https://github.com/EchoStretch/kstuff-lite) — EchoStretch, sleirsgoevy, and contributors
- [cJSON](https://github.com/DaveGamble/cJSON), [7-Zip SDK](https://www.7-zip.org/sdk.html), and Keystone
- All OnionHEN contributors, testers, researchers, and users who provide actionable feedback

<br>

# License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
Third-party components retain their respective licenses and notices.

> OnionHEN is an unofficial homebrew project and is not affiliated with Sony Interactive Entertainment.
> Use it only on hardware you own and at your own risk. No warranty is provided.
