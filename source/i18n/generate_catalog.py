#!/usr/bin/env python3
"""Generate compile-time translation tables from the shared locale JSON."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


SECTIONS = ("toolbox", "notifications")
PRINTF_CONVERSION = re.compile(
    r"%(?:\d+\$)?[-+ #0']*(?:\*|\d+)?(?:\.(?:\*|\d+))?"
    r"(?:hh|h|ll|l|j|z|t|L)?([diuoxXfFeEgGaAcspn%])"
)


def load_locale(path: Path) -> dict[str, dict[str, str]]:
    try:
        data: Any = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"error: cannot load {path}: {error}") from error

    if not isinstance(data, dict) or set(data) != set(SECTIONS):
        raise SystemExit(
            f"error: {path} must contain exactly these sections: "
            f"{', '.join(SECTIONS)}"
        )

    for section in SECTIONS:
        values = data[section]
        if not isinstance(values, dict) or not values:
            raise SystemExit(
                f"error: {path} section {section!r} must be a non-empty object"
            )
        invalid = [
            key
            for key, value in values.items()
            if not isinstance(key, str)
            or not key
            or not isinstance(value, str)
            or "\0" in value
        ]
        if invalid:
            raise SystemExit(
                f"error: {path} section {section!r} has invalid entries: "
                f"{invalid}"
            )
    return data


def printf_conversions(value: str) -> list[str]:
    return [item for item in PRINTF_CONVERSION.findall(value) if item != "%"]


def validate_locales(
    zh_cn: dict[str, dict[str, str]],
    en_us: dict[str, dict[str, str]],
) -> None:
    for section in SECTIONS:
        zh_keys = set(zh_cn[section])
        en_keys = set(en_us[section])
        if zh_keys == en_keys:
            continue
        details = []
        missing_en = sorted(zh_keys - en_keys)
        missing_zh = sorted(en_keys - zh_keys)
        if missing_en:
            details.append(f"missing from en-US.json: {', '.join(missing_en)}")
        if missing_zh:
            details.append(f"missing from zh-CN.json: {', '.join(missing_zh)}")
        raise SystemExit(
            f"error: locale key mismatch in {section}; " + "; ".join(details)
        )

    for section in SECTIONS:
        for key, english in en_us[section].items():
            chinese = zh_cn[section][key]
            if printf_conversions(english) != printf_conversions(chinese):
                raise SystemExit(
                    "error: printf conversions differ for "
                    f"{section} {key!r}: {printf_conversions(english)} != "
                    f"{printf_conversions(chinese)}"
                )


def cpp_string(value: str) -> str:
    # JSON escaping is compatible with the C/C++ string literals emitted here.
    return json.dumps(value, ensure_ascii=False)


def generate(
    catalog: str,
    zh_cn: dict[str, dict[str, str]],
    en_us: dict[str, dict[str, str]],
) -> str:
    lines = [
        "// Generated from source/i18n locale catalogs. Do not edit.",
    ]
    if catalog == "toolbox":
        lines.append("constexpr Entry kTable[] = {")
        for key, chinese in zh_cn["toolbox"].items():
            lines.append(
                f"  {{{cpp_string(key)}, {cpp_string(chinese)}, "
                f"{cpp_string(en_us['toolbox'][key])}}},"
            )
    else:
        lines.append("static const notify_translation_t kTranslations[] = {")
        for key, chinese in zh_cn["notifications"].items():
            lines.append(
                f"    {{{cpp_string(key)}, {cpp_string(chinese)}, "
                f"{cpp_string(en_us['notifications'][key])}}},"
            )
    lines.extend(["};", ""])
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--zh-cn", required=True, type=Path)
    parser.add_argument("--en-us", required=True, type=Path)
    parser.add_argument(
        "--catalog", required=True, choices=("toolbox", "notifications")
    )
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    zh_cn = load_locale(args.zh_cn)
    en_us = load_locale(args.en_us)
    validate_locales(zh_cn, en_us)
    content = generate(args.catalog, zh_cn, en_us)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists() and args.output.read_text(encoding="utf-8") == content:
        return
    args.output.write_text(content, encoding="utf-8")


if __name__ == "__main__":
    main()
