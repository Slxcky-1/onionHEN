#!/usr/bin/env python3
"""Extract and validate RNPS/HBC dump fingerprints used by OnionHEN profiles."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any


HERMES_MAGIC = bytes([0xC6, 0x1F, 0xBC, 0x03, 0xC1, 0x03, 0x19, 0x1F])
LEGACY_BUNDLE_MAGIC = bytes([0xE5, 0xD1, 0x0B, 0xFB])
RNPS_MAGIC = b"RNPSHEDR"
RNPS_PAYLOAD_OFFSET_FIELD = 0x1C
RNPS_FALLBACK_PAYLOAD_OFFSET = 0xB20
HBC_VERSION_OFFSET = 0x08
HBC_SOURCE_HASH_OFFSET = 0x0C
HBC_SOURCE_HASH_SIZE = 20
HBC_FILE_LENGTH_OFFSET = 0x20


KNOWN_HOMEUI_PROFILES = [
    {
        "name": "9.00 NPXS40002 HomeUI",
        "hbc_version": 89,
        "file_length": 0x1846E0,
        "source_hash": "587635687e0a190e38425232c39092888da5adbe",
    },
    {
        "name": "10.01 NPXS40002 HomeUI",
        "hbc_version": 89,
        "file_length": 0x1B3318,
        "source_hash": "ae256655beaeea9e752e23256bbdddf1af254aea",
    },
    {
        # 10.2 NPXS40002 is byte-identical to the 10.4/10.6 dump.
        "name": "10.4/10.6 NPXS40002 HomeUI",
        "hbc_version": 89,
        "file_length": 0x1B3BCC,
        "source_hash": "2cac5cc444ba0473ea8ee632a7942f281482a68a",
    },
    {
        "name": "11.0 NPXS40002 HomeUI",
        "hbc_version": 89,
        "file_length": 0x1B3010,
        "source_hash": "e21110895e8fb6c85f49db972d51fc101bb8fc52",
    },
    {
        # 11.2 NPXS40002 is byte-identical to the 11.4/11.6 dump.
        "name": "11.4/11.6 NPXS40002 HomeUI",
        "hbc_version": 89,
        "file_length": 0x1B2CC8,
        "source_hash": "f321f83f9143035f5d97ee5ad98ceb75133c890e",
    },
    {
        # 12.4 and 12.6 NPXS40002 are byte-identical to the 12.7 dump.
        "name": "12.7 NPXS40002 HomeUI",
        "hbc_version": 89,
        "file_length": 0x1B73BC,
        "source_hash": "9dd2dc47c6024843f685af80ae9273e6a075337d",
    },
    {
        # 12.0 and 12.02 NPXS40002 dumps are byte-identical to 12.20.
        "name": "12.20 NPXS40002 HomeUI",
        "hbc_version": 89,
        "file_length": 0x1B70E4,
        "source_hash": "d9aa3ec2fcf7cc0bb0a7fe6362079c494948cf5e",
    },
]


KNOWN_LEGACY_HOMEUI_PROFILES = [
    {
        # 4.03, 4.50 and 4.51 NPXS40002 are byte-identical.
        "name": "4.03/4.50/4.51 NPXS40002 legacy HomeUI",
        "payload_magic": LEGACY_BUNDLE_MAGIC.hex(),
        "payload_size": 0x152990,
        "file_size": 0x1534B0,
        "sha256": "6db944372cfe8b7d50328ed4bd47c8cae6917821fb30f20004e7f85c673fe00a",
    },
]


KNOWN_SETTINGS_PROFILES = [
    {
        "name": "10.01 NPXS40008 Settings",
        "route": "standard",
        "file_length": 0x4DDA8C,
        "source_hash": "ad6cf2d6f8974ccd34b14e69bb6e340e8dec5dc5",
    },
    {
        # 10.2 and 10.4 dumps share this Settings bundle fingerprint.
        "name": "10.4 NPXS40008 Settings",
        "route": "standard",
        "file_length": 0x4E089C,
        "source_hash": "abb8fdf5a894ce6fd1e99381d0866b33f279c7b9",
    },
    {
        "name": "10.6 NPXS40008 Settings",
        "route": "standard",
        "file_length": 0x4E0954,
        "source_hash": "31651a188d49b23b7635afa449395e0fbd9f682a",
    },
    {
        "name": "11.0 NPXS40008 Settings",
        "route": "old",
        "file_length": 0x4FA540,
        "source_hash": "1824c9fb562e31eef651bb3874c1c73f7f6e24b0",
    },
    {
        "name": "11.2 NPXS40008 Settings",
        "route": "old",
        "file_length": 0x4F45B8,
        "source_hash": "d03462a912c4b5b8db4a98d044b9d488a2dffc7a",
    },
    {
        "name": "11.4 NPXS40008 Settings",
        "route": "old",
        "file_length": 0x4F45C4,
        "source_hash": "a7b731571f84b6cdaf7c4227a980ba5ee20004a8",
    },
    {
        "name": "11.6 NPXS40008 Settings",
        "route": "old",
        "file_length": 0x4F4BFC,
        "source_hash": "92566124b6cfe0b0a7c812fc8a3bbfcf32ac4683",
    },
    {
        # 12.0 and 12.02 dumps share this Settings bundle fingerprint.
        "name": "12.02 NPXS40008 Settings",
        "route": "old",
        "file_length": 0x4E7BEC,
        "source_hash": "fc7c4f15af42929e1d52420c2d174944b4a88043",
    },
    {
        "name": "12.6 NPXS40008 Settings",
        "route": "old",
        "file_length": 0x4E9028,
        "source_hash": "75747bb5fa7e3a4e22d557882f5281e4d1f12959",
    },
    {
        "name": "12.7 NPXS40008 Settings",
        "route": "old",
        "file_length": 0x4E9048,
        "source_hash": "445da8bcba93da165473d3da491d9b13f96316cd",
    },
    {
        # 12.4 and 12.20 dumps share this Settings bundle fingerprint.
        "name": "12.20 NPXS40008 Settings",
        "route": "old",
        "file_length": 0x4E8E54,
        "source_hash": "5d4461858b0a38fc6e7b086dbdfdab619515908e",
    },
]


TRACKED_STRINGS = [
    b"NPXS40002",
    b"ApplicationErrorEventTrigger",
    b"pshomeui:navigateToHome",
    b"download_error",
    b"homeui ApplicationErrorEvent test",
    b"Trigger AppError",
    b"Fps",
    b"pssettings:play",
    b"debug_settings_old",
    b"debug_settings",
    b"icon_setting",
]


def read_u32le(data: bytes, offset: int) -> int | None:
    if offset < 0 or offset + 4 > len(data):
        return None
    return int.from_bytes(data[offset : offset + 4], "little")


def find_all(data: bytes, needle: bytes) -> list[int]:
    offsets: list[int] = []
    start = 0
    while True:
        offset = data.find(needle, start)
        if offset < 0:
            return offsets
        offsets.append(offset)
        start = offset + 1


def locate_hbc(data: bytes) -> tuple[int, str] | tuple[None, str]:
    if data.startswith(HERMES_MAGIC):
        return 0, "direct"

    if data.startswith(RNPS_MAGIC):
        declared = read_u32le(data, RNPS_PAYLOAD_OFFSET_FIELD)
        if declared is not None and 0 < declared < len(data):
            if data[declared : declared + len(HERMES_MAGIC)] == HERMES_MAGIC:
                return declared, "rnps-declared"

        if data[
            RNPS_FALLBACK_PAYLOAD_OFFSET : RNPS_FALLBACK_PAYLOAD_OFFSET
            + len(HERMES_MAGIC)
        ] == HERMES_MAGIC:
            return RNPS_FALLBACK_PAYLOAD_OFFSET, "rnps-fallback"

    offset = data.find(HERMES_MAGIC)
    if offset >= 0:
        return offset, "scan"

    return None, "missing"


def locate_legacy_bundle(data: bytes) -> tuple[int, str] | tuple[None, str]:
    """Locate the pre-Hermes RNPS JavaScript payload used by 4.x HomeUI."""
    if data.startswith(LEGACY_BUNDLE_MAGIC):
        return 0, "direct-legacy"

    if data.startswith(RNPS_MAGIC):
        declared = read_u32le(data, RNPS_PAYLOAD_OFFSET_FIELD)
        candidates = []
        if declared is not None and 0 < declared < len(data):
            candidates.append((declared, "rnps-declared-legacy"))
        candidates.append((RNPS_FALLBACK_PAYLOAD_OFFSET, "rnps-fallback-legacy"))
        for offset, location in candidates:
            if (
                data[offset : offset + len(LEGACY_BUNDLE_MAGIC)]
                == LEGACY_BUNDLE_MAGIC
            ):
                return offset, location

    return None, "missing"


def match_legacy_profile(data: bytes, payload_offset: int) -> dict[str, Any] | None:
    payload = data[payload_offset:]
    digest = hashlib.sha256(data).hexdigest()
    for profile in KNOWN_LEGACY_HOMEUI_PROFILES:
        if len(data) != profile["file_size"]:
            continue
        if len(payload) != profile["payload_size"]:
            continue
        if payload[:4].hex() != profile["payload_magic"]:
            continue
        if digest != profile["sha256"]:
            continue
        return profile
    return None


def match_profile(
    profiles: list[dict[str, Any]],
    hbc_version: int | None,
    file_length: int | None,
    source_hash: str,
) -> dict[str, Any] | None:
    for profile in profiles:
        if profile["file_length"] != file_length:
            continue
        if profile["source_hash"] != source_hash:
            continue
        expected_version = profile.get("hbc_version")
        if expected_version is not None and expected_version != hbc_version:
            continue
        return profile
    return None


def infer_settings_route(hbc: bytes) -> str:
    if b"debug_settings_old" in hbc:
        return "old"
    if b"debug_settings" in hbc:
        return "standard"
    return "unknown"


def analyze_file(path: Path, app_id: str) -> dict[str, Any]:
    data = path.read_bytes()
    hbc_offset, hbc_location = locate_hbc(data)
    result: dict[str, Any] = {
        "app_id": app_id,
        "path": str(path),
        "size": path.stat().st_size,
        "hbc_offset": hbc_offset,
        "hbc_location": hbc_location,
    }

    if app_id == "NPXS40002":
        legacy_offset, legacy_location = locate_legacy_bundle(data)
        if legacy_offset is not None:
            payload = data[legacy_offset:]
            profile = match_legacy_profile(data, legacy_offset)
            result.update(
                {
                    "legacy_offset": legacy_offset,
                    "legacy_location": legacy_location,
                    "legacy_payload_size": len(payload),
                    "legacy_magic": payload[:4].hex(),
                    "legacy_sha256": hashlib.sha256(data).hexdigest(),
                    "profile": profile["name"] if profile else None,
                    "supported": profile is not None,
                    "strings": {},
                }
            )
            for needle in TRACKED_STRINGS:
                offsets = find_all(payload, needle)
                result["strings"][needle.decode("ascii")] = {
                    "count": len(offsets),
                    "first_offsets": [f"0x{offset:x}" for offset in offsets[:8]],
                }
            return result

    if hbc_offset is None:
        result["supported"] = False
        result["error"] = "HBC magic not found"
        return result

    hbc = data[hbc_offset:]
    hbc_version = read_u32le(hbc, HBC_VERSION_OFFSET)
    file_length = read_u32le(hbc, HBC_FILE_LENGTH_OFFSET)
    source_hash = hbc[
        HBC_SOURCE_HASH_OFFSET : HBC_SOURCE_HASH_OFFSET + HBC_SOURCE_HASH_SIZE
    ].hex()
    profile_table = (
        KNOWN_HOMEUI_PROFILES if app_id == "NPXS40002" else KNOWN_SETTINGS_PROFILES
    )
    profile = match_profile(profile_table, hbc_version, file_length, source_hash)

    result.update(
        {
            "hbc_version": hbc_version,
            "hbc_file_length": file_length,
            "hbc_file_length_hex": f"0x{file_length:x}" if file_length else None,
            "source_hash": source_hash,
            "profile": profile["name"] if profile else None,
            "supported": profile is not None,
            "strings": {},
        }
    )

    strings: dict[str, Any] = {}
    for needle in TRACKED_STRINGS:
        offsets = find_all(hbc, needle)
        strings[needle.decode("ascii")] = {
            "count": len(offsets),
            "first_offsets": [f"0x{offset:x}" for offset in offsets[:8]],
        }
    result["strings"] = strings

    if app_id == "NPXS40008":
        route = infer_settings_route(hbc)
        result["settings_route"] = route
        result["profile_route"] = profile.get("route") if profile else None
        result["route_matches_profile"] = bool(
            profile and profile.get("route") == route
        )

    return result


def analyze_dump(dump_dir: Path) -> dict[str, Any]:
    apps: list[dict[str, Any]] = []
    for app_id in ("NPXS40002", "NPXS40008"):
        path = dump_dir / f"{app_id}.bin"
        if path.exists():
            apps.append(analyze_file(path, app_id))

    return {
        "dump_dir": str(dump_dir),
        "apps": apps,
    }


def validation_errors(report: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    apps = report["apps"]
    if not apps:
        errors.append("no NPXS40002.bin or NPXS40008.bin found")
        return errors

    for app in apps:
        if not app.get("supported"):
            errors.append(f"{app['app_id']} has no matching known profile")
            continue
        if app["app_id"] == "NPXS40008" and not app.get("route_matches_profile"):
            errors.append(
                f"{app['app_id']} route {app.get('settings_route')} does not "
                f"match profile route {app.get('profile_route')}"
            )
    return errors


def print_text(report: dict[str, Any], errors: list[str]) -> None:
    print(f"Dump: {report['dump_dir']}")
    for app in report["apps"]:
        print(f"\n{app['app_id']}: {app['path']}")
        print(f"  file_size: 0x{app['size']:x} ({app['size']})")
        if app.get("legacy_offset") is not None:
            print(
                "  legacy RNPS bundle: "
                f"offset=0x{app['legacy_offset']:x} "
                f"location={app['legacy_location']} "
                f"magic={app['legacy_magic']} "
                f"payload_size=0x{app['legacy_payload_size']:x} "
                f"sha256={app['legacy_sha256']}"
            )
            print(
                "  profile: "
                f"{app['profile'] if app['profile'] else 'unsupported'} "
                f"(supported={'yes' if app['supported'] else 'no'})"
            )
            print("  strings:")
            for name, detail in app["strings"].items():
                if detail["count"] == 0:
                    continue
                offsets = ", ".join(detail["first_offsets"])
                print(f"    {name}: count={detail['count']} first={offsets}")
            continue
        if app.get("hbc_offset") is None:
            print(f"  hbc: missing ({app.get('error')})")
            continue
        print(
            "  hbc: "
            f"offset=0x{app['hbc_offset']:x} "
            f"location={app['hbc_location']} "
            f"version={app['hbc_version']} "
            f"file_length={app['hbc_file_length_hex']} "
            f"source_hash={app['source_hash']}"
        )
        print(
            "  profile: "
            f"{app['profile'] if app['profile'] else 'unsupported'} "
            f"(supported={'yes' if app['supported'] else 'no'})"
        )
        if app["app_id"] == "NPXS40008":
            print(
                "  settings_route: "
                f"{app.get('settings_route')} "
                f"(profile={app.get('profile_route')})"
            )
        print("  strings:")
        for name, detail in app["strings"].items():
            if detail["count"] == 0:
                continue
            offsets = ", ".join(detail["first_offsets"])
            print(f"    {name}: count={detail['count']} first={offsets}")

    if errors:
        print("\nValidation: FAIL")
        for error in errors:
            print(f"  - {error}")
    else:
        print("\nValidation: PASS")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Extract RNPS/HBC fingerprints and validate known profiles."
    )
    parser.add_argument("dump_dir", type=Path, help="Directory containing NPXS*.bin")
    parser.add_argument("--json", action="store_true", help="Emit machine JSON")
    parser.add_argument(
        "--allow-unsupported",
        action="store_true",
        help="Return success even when a profile is unknown",
    )
    args = parser.parse_args(argv)

    if not args.dump_dir.is_dir():
        print(f"error: not a directory: {args.dump_dir}", file=sys.stderr)
        return 2

    report = analyze_dump(args.dump_dir)
    errors = validation_errors(report)
    if args.json:
        print(json.dumps({**report, "validation_errors": errors}, indent=2))
    else:
        print_text(report, errors)

    if errors and not args.allow_unsupported:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
