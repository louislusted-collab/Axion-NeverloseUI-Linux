#!/usr/bin/env python3
"""Validate an Axion native-Linux signature manifest without applying it."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

PATTERN = re.compile(r"^(?:[0-9A-Fa-f]{2}|\?\?|\?)(?:\s+(?:[0-9A-Fa-f]{2}|\?\?|\?))*$")
MODULES = {
    "libclient.so",
    "libengine2.so",
    "libinputsystem.so",
    "libparticles.so",
    "libscenesystem.so",
    "libschemasystem.so",
    "libtier0.so",
}
OPERATIONS = {"add", "sub", "rip", "read", "slice"}


def fail(message: str) -> None:
    raise ValueError(message)


def validate(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        fail("manifest root must be an object")
    if data.get("format") != 1:
        fail("unsupported manifest format")
    if data.get("platform") != "linux-x86_64-native":
        fail("manifest is not for native Linux x86_64")

    schema = data.get("schema")
    if not isinstance(schema, dict) or schema.get("mode") != "runtime":
        fail("schema mode must be runtime")

    signatures = data.get("signatures")
    if not isinstance(signatures, dict):
        fail("signatures must be an object")

    for name, entry in signatures.items():
        if not isinstance(name, str) or not name:
            fail("signature names must be non-empty strings")
        if not isinstance(entry, dict):
            fail(f"{name}: entry must be an object")
        if entry.get("module") not in MODULES:
            fail(f"{name}: unsupported Linux module")
        pattern = entry.get("pattern")
        if not isinstance(pattern, str) or not PATTERN.fullmatch(pattern.strip()):
            fail(f"{name}: invalid IDA pattern")
        if not isinstance(entry.get("occurrences", 1), int) or entry.get("occurrences", 1) != 1:
            fail(f"{name}: pattern must require exactly one match")
        if not isinstance(entry.get("offset", 0), int):
            fail(f"{name}: offset must be an integer")
        operations = entry.get("operations", [])
        if not isinstance(operations, list):
            fail(f"{name}: operations must be an array")
        for operation in operations:
            if not isinstance(operation, dict) or operation.get("type") not in OPERATIONS:
                fail(f"{name}: invalid operation")
            kind = operation["type"]
            if kind in {"add", "sub"} and not isinstance(operation.get("value"), int):
                fail(f"{name}: {kind} requires an integer value")
            if kind == "slice" and not all(isinstance(operation.get(key), int) for key in ("start", "end")):
                fail(f"{name}: slice requires integer start/end")

    return data


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} MANIFEST", file=sys.stderr)
        return 2
    try:
        data = validate(Path(sys.argv[1]))
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"invalid offset manifest: {error}", file=sys.stderr)
        return 1
    print(f"valid native Linux manifest: {len(data['signatures'])} signatures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
