#!/usr/bin/env python3
"""Convert cs2-dumper's Linux offsets.json to Axion's runtime cache format."""

import argparse
import json
from pathlib import Path


ALIASES = {
    "dwViewMatrix": "dwViewMatrixNative",
    "dwLocalPlayerController": "dwLocalPlayerControllerNative",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    source = json.loads(args.input.read_text(encoding="utf-8"))
    resolved = {
        "format": 1,
        "platform": "linux-x86_64-native",
        "source": "catpetter1999/cs2-dumper linux branch",
        "modules": {},
        "offsets": {},
    }

    for module, entries in source.items():
        if not isinstance(entries, list):
            continue
        for entry in entries:
            name = entry.get("name")
            value = entry.get("value")
            if not isinstance(name, str) or not isinstance(value, int):
                continue
            resolved["offsets"][name] = {"module": module, "value": value}
            alias = ALIASES.get(name)
            if alias:
                resolved["offsets"][alias] = {"module": module, "value": value}

    required = ("dwViewMatrixNative", "dwLocalPlayerControllerNative")
    missing = [name for name in required if name not in resolved["offsets"]]
    if missing:
        raise SystemExit("full dump is missing required offsets: " + ", ".join(missing))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(json.dumps(resolved, indent=2) + "\n", encoding="utf-8")
    temporary.replace(args.output)
    print(f"Imported {len(resolved['offsets'])} live Linux offsets into Axion.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
