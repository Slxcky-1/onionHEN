#!/usr/bin/env python3
"""Generate obfuscated C arrays for sensitive user-facing strings.

Runtime decode (toolbox_xml.cpp / libonion_platform/obf_str.c):
  plain[i] = (enc[i] - (i*17 + 31)) ^ key[i % len(key)]
  key = base64.b64decode(KEY_B64)

Usage:
  python3 encrypt_banner.py              # toolbox beta banner only
  python3 encrypt_banner.py --all        # banner + boot notify messages
"""

from __future__ import annotations

import argparse
import base64
import sys

KEY_B64 = b"U0lTVFIwX0lfU0VFX1lPVQ=="

DEFAULT_BANNER_ZH = "★ 内测 %s · 禁止外传 · 若付费购买请退款 ★"
DEFAULT_BANNER_EN = "★ BETA %s · DO NOT REDISTRIBUTE · If paid, request a refund ★"

NOTIFY_MSGS = {
    "kIntegrityEn": "Integrity check failed\nThis build is corrupted or modified",
    "kIntegrityZh": "完整性校验失败\n此版本已损坏或被修改",
    "kRedistribEn": (
        "This is a beta build. Redistribution is prohibited.\n"
        "If you paid for this plugin, please request a refund."
    ),
    "kRedistribZh": "本版本为内测版本，禁止外传。\n如果您付费购买了此插件，请退款。",
}


def encode(data: bytes, key: bytes) -> bytes:
    out = bytearray()
    for i, b in enumerate(data):
        x = b ^ key[i % len(key)]
        x = (x + (i * 17 + 31)) & 0xFF
        out.append(x)
    return bytes(out)


def decode(data: bytes, key: bytes) -> bytes:
    out = bytearray()
    for i, b in enumerate(data):
        x = (b - (i * 17 + 31)) & 0xFF
        x ^= key[i % len(key)]
        out.append(x)
    return bytes(out)


def c_array(name: str, data: bytes, *, c_style: bool) -> str:
    decl = (
        f"static const uint8_t {name}[] = {{"
        if c_style
        else f"constexpr unsigned char {name}[] = {{"
    )
    lines = [decl]
    row: list[str] = []
    for i, b in enumerate(data):
        row.append(f"0x{b:02x}")
        if len(row) == 12 or i + 1 == len(data):
            suffix = "," if i + 1 < len(data) else ""
            lines.append("    " + ", ".join(row) + suffix)
            row = []
    lines.append("};")
    return "\n".join(lines)


def emit(name: str, plain: str, key: bytes, *, c_style: bool, require_pct_s: bool) -> None:
    if require_pct_s and "%s" not in plain:
        raise SystemExit(f"error: {name} must contain %s")
    raw = plain.encode("utf-8")
    enc = encode(raw, key)
    assert decode(enc, key) == raw, f"{name} round-trip failed"
    print(f"/* Obfuscated {name}, len={len(enc)} */")
    print(c_array(name, enc, c_style=c_style))
    print()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Obfuscate beta banner / boot notify format strings"
    )
    parser.add_argument(
        "--zh",
        default=DEFAULT_BANNER_ZH,
        help="zh-Hans banner format (must contain %%s for version)",
    )
    parser.add_argument(
        "--en",
        default=DEFAULT_BANNER_EN,
        help="en-US banner format (must contain %%s for version)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Also emit integrity/redistrib notify blobs for obf_str.c",
    )
    args = parser.parse_args()

    key = base64.b64decode(KEY_B64)

    print("/* --- toolbox_xml.cpp beta banner --- */")
    emit("kBetaBannerZhEnc", args.zh, key, c_style=False, require_pct_s=True)
    emit("kBetaBannerEnEnc", args.en, key, c_style=False, require_pct_s=True)

    if args.all:
        print("/* --- libonion_platform/source/obf_str.c --- */")
        for name, plain in NOTIFY_MSGS.items():
            emit(name, plain, key, c_style=True, require_pct_s=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
