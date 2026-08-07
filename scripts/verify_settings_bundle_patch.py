#!/usr/bin/env python3
"""Verify the Hermes Settings patch against the 9.00 and 11.6 dumps.

The runtime patch must preserve Hermes shared string storage: only the Debug
Settings label and the HBC footer may change. The Settings icon is deployed at
the stock icon_setting.png path by the bootstrapper.
"""

from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
DUMP_ROOT = Path("/Users/chenpy/Projects/Person/ps5-kylin/Sony Dumps")
VERSIONS = ("9.00", "11.6")
HERMES_MAGIC = bytes.fromhex("c61fbc03c103191f")
FOOTER_SIZE = 20
OLD_LABEL = ("\u2605Debug Settings").encode("utf-16le")
NEW_LABEL = ("\u2605OnionHEN Tools").encode("utf-16le")
COMMON_PROTECTED_STRINGS = (
    b"icon_setting",
    b"_settingInstance",
)
VERSION_PROTECTED_STRINGS = {
    "9.00": (
        b"assets/src/modules/devices/hunt/buttonAssignments/assets/icon",
    ),
    "11.6": (b"avatar-appear-offline-icon",),
}


def find_all(data: bytes, needle: bytes) -> list[int]:
    offsets: list[int] = []
    start = 0
    while True:
        offset = data.find(needle, start)
        if offset < 0:
            return offsets
        offsets.append(offset)
        start = offset + 1


def load_hbc(version: str) -> bytearray:
    path = DUMP_ROOT / version / "NPXS40008.bin"
    raw = path.read_bytes()
    hbc_offset = raw.find(HERMES_MAGIC)
    if hbc_offset < 0:
        raise ValueError(f"{version}: Hermes magic not found")
    available = raw[hbc_offset:]
    if len(available) < 0x24:
        raise ValueError(f"{version}: truncated HBC header")
    file_length = struct.unpack_from("<I", available, 0x20)[0]
    if file_length < FOOTER_SIZE or file_length > len(available):
        raise ValueError(f"{version}: invalid HBC file length 0x{file_length:x}")
    return bytearray(available[:file_length])


def footer_is_valid(hbc: bytes) -> bool:
    footer_offset = len(hbc) - FOOTER_SIZE
    return hashlib.sha1(hbc[:footer_offset]).digest() == hbc[footer_offset:]


def apply_settings_patch(hbc: bytearray) -> bool:
    label_offset = hbc.find(OLD_LABEL)
    if label_offset < 0:
        return False
    hbc[label_offset : label_offset + len(OLD_LABEL)] = NEW_LABEL
    footer_offset = len(hbc) - FOOTER_SIZE
    hbc[footer_offset:] = hashlib.sha1(hbc[:footer_offset]).digest()
    return True


def verify_version(version: str) -> list[str]:
    errors: list[str] = []
    original = load_hbc(version)
    if not footer_is_valid(original):
        errors.append("stock footer SHA-1 is invalid")
    if original.count(OLD_LABEL) != 1:
        errors.append(f"stock label count is {original.count(OLD_LABEL)}, expected 1")

    protected_strings = (
        COMMON_PROTECTED_STRINGS + VERSION_PROTECTED_STRINGS[version]
    )
    protected_offsets = {
        needle: find_all(original, needle) for needle in protected_strings
    }
    for needle, offsets in protected_offsets.items():
        if not offsets:
            errors.append(f"stock protected string missing: {needle!r}")

    icon_offset = original.find(b"icon_setting")
    icon_range = range(icon_offset, icon_offset + len(b"icon_setting"))
    for collateral in VERSION_PROTECTED_STRINGS[version] + (b"_settingInstance",):
        collateral_offset = original.find(collateral)
        collateral_range = range(
            collateral_offset, collateral_offset + len(collateral)
        )
        if (
            icon_range.stop <= collateral_range.start
            or collateral_range.stop <= icon_range.start
        ):
            errors.append(f"expected shared storage not found: {collateral!r}")

    patched = bytearray(original)
    changed = apply_settings_patch(patched)
    if not changed:
        errors.append("first patch did not replace the label")
        return errors
    if not footer_is_valid(patched):
        errors.append("patched footer SHA-1 is invalid")

    label_offset = original.find(OLD_LABEL)
    footer_offset = len(original) - FOOTER_SIZE
    allowed = set(range(label_offset, label_offset + len(OLD_LABEL)))
    allowed.update(range(footer_offset, len(original)))
    unexpected = [
        offset
        for offset, (old, new) in enumerate(zip(original, patched))
        if old != new and offset not in allowed
    ]
    if unexpected:
        errors.append(
            f"bytes changed outside label/footer, first offset=0x{unexpected[0]:x}"
        )

    for needle, offsets in protected_offsets.items():
        if find_all(patched, needle) != offsets:
            errors.append(f"protected string changed: {needle!r}")
    if b"onionh_sicon" in patched:
        errors.append("deprecated onionh_sicon id appeared in Hermes HBC")

    second_pass = bytearray(patched)
    if apply_settings_patch(second_pass):
        errors.append("second patch unexpectedly replaced another label")
    if second_pass != patched:
        errors.append("second patch was not idempotent")

    print(
        f"[{'PASS' if not errors else 'FAIL'}] {version} NPXS40008 "
        f"footer={patched[-FOOTER_SIZE:].hex()}"
    )
    return errors


def verify_source_contract() -> list[str]:
    errors: list[str] = []
    hook = (REPO / "source/shellui/src/hook_functions.cpp").read_text()
    bootstrapper = (REPO / "source/bootstrapper/source/main.cpp").read_text()
    if 'replace_all(buffer, size_ptr, buffer_capacity, "icon_setting"' in hook:
        errors.append("Hermes icon_setting replacement is still present")
    if "texture/icon_setting.png" not in bootstrapper:
        errors.append("bootstrapper does not deploy the stock Settings icon path")
    return errors


def main() -> int:
    all_errors = verify_source_contract()
    for version in VERSIONS:
        try:
            errors = verify_version(version)
        except (OSError, ValueError, struct.error) as exc:
            errors = [str(exc)]
        all_errors.extend(f"{version}: {error}" for error in errors)

    if all_errors:
        for error in all_errors:
            print(f"ERROR: {error}")
        print("OVERALL: FAIL")
        return 1
    print("OVERALL: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
