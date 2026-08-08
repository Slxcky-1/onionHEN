#!/usr/bin/env python3
"""Verify Hermes and legacy Settings patches against representative dumps.

The runtime patch must preserve Hermes shared string storage: only the Debug
Settings label and the HBC footer may change. The Settings icon is deployed at
the stock icon_setting.png path by the bootstrapper. Legacy bundles use
profiled, equal-length UTF-8 label and icon replacements.
"""

from __future__ import annotations

import hashlib
import re
import struct
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
DUMP_ROOT = Path("/Users/chenpy/Projects/Person/ps5-kylin/Sony Dumps")
SETTINGS_CPP = (
    REPO / "source/shellui/src/settings_bundle_patch.cpp"
).read_text()
HERMES_VERSIONS = ("9.00", "fw9.4", "fw9.6", "11.6")
LEGACY_VERSIONS = (
    "4.03",
    "4.50",
    "4.51",
    "5.1",
    "6.0",
    "6.02",
    "7.4",
    "7.61",
    "8.0",
    "8.4",
    "fw8.6",
)
HERMES_MAGIC = bytes.fromhex("c61fbc03c103191f")
LEGACY_MAGIC = bytes.fromhex("e5d10bfb")
FOOTER_SIZE = 20
OLD_LABEL = ("\u2605Debug Settings").encode("utf-16le")
NEW_LABEL = ("\u2605OnionHEN Tools").encode("utf-16le")
LEGACY_OLD_LABEL = ("\u2605Debug Settings").encode()
LEGACY_NEW_LABEL = ("\u2605OnionHEN Tools").encode()
LEGACY_OLD_ICON = b"icon_setting"
LEGACY_NEW_ICON = b"onionh_sicon"
COMMON_PROTECTED_STRINGS = (
    b"icon_setting",
    b"_settingInstance",
)
VERSION_PROTECTED_STRINGS = {
    "9.00": (
        b"assets/src/modules/devices/hunt/buttonAssignments/assets/icon",
    ),
    "fw9.4": (
        b"assets/src/modules/devices/hunt/buttonAssignments/assets/icon",
    ),
    "fw9.6": (
        b"assets/src/modules/devices/hunt/buttonAssignments/assets/icon",
    ),
    "11.6": (b"avatar-appear-offline-icon",),
}


def load_legacy_profiles() -> list[dict]:
    table = re.search(
        r"kLegacySettingsProfiles\[\] = \{(.*?)\n\};", SETTINGS_CPP, re.S
    )
    if not table:
        raise SystemExit("legacy Settings profile table not found")
    profiles = []
    for name, payload_size, label_offset, icon_offset in re.findall(
        r'\{"([^"]+)",\s*(0x[0-9a-fA-F]+),\s*'
        r'(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\}',
        table.group(1),
    ):
        profiles.append(
            {
                "name": name,
                "payload_size": int(payload_size, 16),
                "label_offset": int(label_offset, 16),
                "icon_offset": int(icon_offset, 16),
            }
        )
    if not profiles:
        raise SystemExit("no legacy Settings profiles parsed")
    return profiles


LEGACY_PROFILES = load_legacy_profiles()


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


def verify_hermes_version(version: str) -> list[str]:
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


def locate_legacy_payload(data: bytes | bytearray) -> int | None:
    if data[: len(LEGACY_MAGIC)] == LEGACY_MAGIC:
        return 0
    if data[:8] != b"RNPSHEDR" or len(data) < 0x20:
        return None
    declared = struct.unpack_from("<I", data, 0x1C)[0]
    for offset in dict.fromkeys((declared, 0xB20, 0xB30)):
        if 0 < offset <= len(data) - len(LEGACY_MAGIC):
            if data[offset : offset + len(LEGACY_MAGIC)] == LEGACY_MAGIC:
                return offset
    return None


def match_legacy_profile(payload: bytes | bytearray) -> dict | None:
    for profile in LEGACY_PROFILES:
        if len(payload) != profile["payload_size"]:
            continue
        label_offset = profile["label_offset"]
        icon_offset = profile["icon_offset"]
        label = bytes(payload[label_offset : label_offset + len(LEGACY_OLD_LABEL)])
        icon = bytes(payload[icon_offset : icon_offset + len(LEGACY_OLD_ICON)])
        if label not in (LEGACY_OLD_LABEL, LEGACY_NEW_LABEL):
            continue
        if icon not in (LEGACY_OLD_ICON, LEGACY_NEW_ICON):
            continue
        return profile
    return None


def apply_legacy_settings(
    data: bytearray,
) -> tuple[dict | None, int | None, int, int]:
    payload_offset = locate_legacy_payload(data)
    if payload_offset is None:
        return None, None, 0, 0
    payload = data[payload_offset:]
    profile = match_legacy_profile(payload)
    if profile is None:
        return None, payload_offset, 0, 0

    label_count = 0
    icon_count = 0
    label_offset = payload_offset + profile["label_offset"]
    icon_offset = payload_offset + profile["icon_offset"]
    if data[label_offset : label_offset + len(LEGACY_OLD_LABEL)] == LEGACY_OLD_LABEL:
        data[label_offset : label_offset + len(LEGACY_OLD_LABEL)] = LEGACY_NEW_LABEL
        label_count = 1
    if data[icon_offset : icon_offset + len(LEGACY_OLD_ICON)] == LEGACY_OLD_ICON:
        data[icon_offset : icon_offset + len(LEGACY_OLD_ICON)] = LEGACY_NEW_ICON
        icon_count = 1
    return profile, payload_offset, label_count, icon_count


def verify_legacy_version(version: str) -> list[str]:
    errors: list[str] = []
    path = DUMP_ROOT / version / "NPXS40008.bin"
    original = path.read_bytes()
    payload_offset = locate_legacy_payload(original)
    if payload_offset is None:
        return ["legacy payload not found"]
    original_payload = original[payload_offset:]
    profile = match_legacy_profile(original_payload)
    if profile is None:
        return [f"no profile for payload size 0x{len(original_payload):x}"]

    if locate_legacy_payload(original_payload) != 0:
        errors.append("direct legacy payload locator failed")
    old_label_offsets = find_all(original_payload, LEGACY_OLD_LABEL)
    if len(old_label_offsets) != 2:
        errors.append(
            f"stock UTF-8 label count is {len(old_label_offsets)}, expected 2"
        )
    if profile["label_offset"] not in old_label_offsets:
        errors.append("profile does not target a stock Debug Settings label")
    if original_payload.find(LEGACY_OLD_ICON) != profile["icon_offset"]:
        errors.append("profile icon offset does not match stock icon_setting")

    patched = bytearray(original)
    matched, patched_offset, label_count, icon_count = apply_legacy_settings(patched)
    if matched != profile or patched_offset != payload_offset:
        errors.append("container patch matched the wrong profile or payload offset")
    if (label_count, icon_count) != (1, 1):
        errors.append(
            f"first patch counts label={label_count} icon={icon_count}, expected 1/1"
        )
    if len(patched) != len(original):
        errors.append("legacy patch changed container length")

    label_start = payload_offset + profile["label_offset"]
    icon_start = payload_offset + profile["icon_offset"]
    allowed = set(range(label_start, label_start + len(LEGACY_OLD_LABEL)))
    allowed.update(range(icon_start, icon_start + len(LEGACY_OLD_ICON)))
    unexpected = [
        offset
        for offset, (old, new) in enumerate(zip(original, patched))
        if old != new and offset not in allowed
    ]
    if unexpected:
        errors.append(
            f"legacy bytes changed outside label/icon at 0x{unexpected[0]:x}"
        )

    patched_payload = bytes(patched[payload_offset:])
    remaining_old_labels = find_all(patched_payload, LEGACY_OLD_LABEL)
    expected_remaining = [
        offset for offset in old_label_offsets if offset != profile["label_offset"]
    ]
    if remaining_old_labels != expected_remaining:
        errors.append("non-menu Debug Settings text changed")

    direct = bytearray(original_payload)
    _, direct_offset, direct_labels, direct_icons = apply_legacy_settings(direct)
    if direct_offset != 0 or (direct_labels, direct_icons) != (1, 1):
        errors.append("direct payload patch did not apply exactly once")
    if direct != patched[payload_offset:]:
        errors.append("direct and RNPS-container patch results differ")

    second_pass = bytearray(patched)
    _, _, second_labels, second_icons = apply_legacy_settings(second_pass)
    if (second_labels, second_icons) != (0, 0):
        errors.append("legacy second pass was not idempotent")
    if second_pass != patched:
        errors.append("legacy second pass changed bytes")

    print(
        f"[{'PASS' if not errors else 'FAIL'}] {version} NPXS40008 "
        f"profile={profile['name']} label=0x{profile['label_offset']:x} "
        f"icon=0x{profile['icon_offset']:x}"
    )
    return errors


def verify_source_contract() -> list[str]:
    errors: list[str] = []
    bootstrapper = (REPO / "source/bootstrapper/source/main.cpp").read_text()
    if (
        'replace_all(buffer, size_ptr, buffer_capacity, "icon_setting"'
        in SETTINGS_CPP
    ):
        errors.append("Hermes icon_setting replacement is still present")
    if "texture/icon_setting.png" not in bootstrapper:
        errors.append("bootstrapper does not deploy the stock Settings icon path")
    return errors


def main() -> int:
    all_errors = verify_source_contract()
    for version in LEGACY_VERSIONS:
        try:
            errors = verify_legacy_version(version)
        except (OSError, ValueError, struct.error) as exc:
            errors = [str(exc)]
        all_errors.extend(f"{version}: {error}" for error in errors)
    for version in HERMES_VERSIONS:
        try:
            errors = verify_hermes_version(version)
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
