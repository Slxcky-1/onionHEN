# Third-party dependencies

Everything in this directory is maintained outside OnionHEN. Keeping external
code here makes `source/` exclusively first-party code.

| Path | Form | Role in OnionHEN |
|------|------|------------------|
| [`7zip_sdk/`](7zip_sdk/) | Vendored source | LZMA support used by the unpacker |
| [`cjson/`](cjson/) | Vendored source | JSON parsing and serialization |
| [`cheat_support/`](cheat_support/) | Vendored source | AES, base64, miniz and SHA-256 used by cheat parsers |
| [`keystone/`](keystone/) | Headers + prebuilt archive | ShnExt assembly support |
| [`kstuff-lite/`](kstuff-lite/) | Git submodule | Produces the optional embedded `kstuff.elf` |
| [`ftpsrv/`](ftpsrv/) | Pinned Git submodule (`nexgen`) | Source-built PS5 FTP server payload; OnionHEN launches it on the built-in port |
| [`ShadowMountPlus/`](ShadowMountPlus/) | Release ELF | Embedded `1.6beta16` payload staged by `scripts/sync_dependencies.sh` |

Third-party file names retain their upstream spelling even when it differs
from the project's snake_case convention. This keeps upstream updates easy to
review.

Source-built or downloaded fallback dependency blobs are cached in
`.cache/dependencies/` and ignored by Git. `scripts/sync_dependencies.sh`
stages the required bootstrapper input from that cache; generated blobs do not
belong in `source/`.

`ftpsrv` comes from the pinned `nexgen` source checkout, or the `1.15-ng-stable`
release ELF if that checkout is missing. ShadowMountPlus is the `1.6beta16`
release ELF.

## Runtime-only external dependency

[ps5-payload-dev/elfldr](https://github.com/ps5-payload-dev/elfldr) on port 9021 is required at runtime. Its source and binary are not vendored or packaged by OnionHEN.

## Removed from OnionHEN

| Former dependency | Reason |
|-------------------|--------|
| Vendored `elfldr.elf` | The port 9021 service is supplied externally at runtime |
| ps5debug / ps5debug-NG | Optional debugger; not embedded |
| ps5-app-dumper | Optional dump payload; not embedded |
| Byepervisor / hen.bin | 1.xx–2.xx HV path; not bundled |
| Discord RPC | Optional util service; removed |
| libSelfDecryptor | Optional alt decrypt; not used |

```bash
git submodule update --init --recursive
./scripts/sync_dependencies.sh
```
