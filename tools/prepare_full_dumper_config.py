#!/usr/bin/env python3
"""Keep only signature modules currently mapped by the native CS2 process."""

import argparse
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("config", type=Path)
    parser.add_argument("maps", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    config = json.loads(args.config.read_text(encoding="utf-8"))
    mapped = set()
    for line in args.maps.read_text(encoding="utf-8", errors="replace").splitlines():
        # /proc/<pid>/maps has five fixed whitespace-delimited columns followed
        # by the pathname. Split only those columns: Steam's default CS2 path
        # contains "Counter-Strike Global Offensive", so split()[-1] loses the
        # absolute path and previously made every live module look unloaded.
        columns = line.split(maxsplit=5)
        if len(columns) == 6 and columns[5].startswith("/"):
            mapped.add(Path(columns[5]).name)

    filtered = []
    for group in config.get("signatures", []):
        kept = {module: signatures for module, signatures in group.items() if module in mapped}
        if kept:
            filtered.append(kept)
    config["signatures"] = filtered

    if not any("libclient.so" in group for group in filtered):
        raise SystemExit("libclient.so is not loaded in the selected CS2 process")

    args.output.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    print("Live modules: " + ", ".join(sorted(module for group in filtered for module in group)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
