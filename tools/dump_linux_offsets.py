#!/usr/bin/env python3
"""Resolve validated Axion signatures directly from native CS2 ELF files."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
from pathlib import Path

DEFAULT_GAME = Path.home() / ".local/share/Steam/steamapps/common/Counter-Strike Global Offensive/game"
MODULE_PATHS = {
    "libclient.so": "csgo/bin/linuxsteamrt64/libclient.so",
    "libengine2.so": "bin/linuxsteamrt64/libengine2.so",
    "libinputsystem.so": "bin/linuxsteamrt64/libinputsystem.so",
    "libparticles.so": "bin/linuxsteamrt64/libparticles.so",
    "libscenesystem.so": "bin/linuxsteamrt64/libscenesystem.so",
    "libschemasystem.so": "bin/linuxsteamrt64/libschemasystem.so",
    "libtier0.so": "bin/linuxsteamrt64/libtier0.so",
}


class ElfImage:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if self.data[:5] != b"\x7fELF\x02":
            raise ValueError(f"{path}: not an ELF64 image")
        endian = "<" if self.data[5] == 1 else ">"
        phoff = struct.unpack_from(endian + "Q", self.data, 32)[0]
        phentsize, phnum = struct.unpack_from(endian + "HH", self.data, 54)
        self.endian = endian
        self.loads: list[tuple[int, int, int]] = []
        for index in range(phnum):
            pos = phoff + index * phentsize
            p_type = struct.unpack_from(endian + "I", self.data, pos)[0]
            if p_type != 1:
                continue
            p_offset, p_vaddr = struct.unpack_from(endian + "QQ", self.data, pos + 8)
            p_filesz = struct.unpack_from(endian + "Q", self.data, pos + 32)[0]
            self.loads.append((p_offset, p_vaddr, p_filesz))

    def file_to_va(self, offset: int) -> int:
        for file_start, va_start, size in self.loads:
            if file_start <= offset < file_start + size:
                return va_start + offset - file_start
        raise ValueError(f"{self.path.name}: file offset 0x{offset:X} is not mapped")

    def va_to_file(self, address: int) -> int:
        for file_start, va_start, size in self.loads:
            if va_start <= address < va_start + size:
                return file_start + address - va_start
        raise ValueError(f"{self.path.name}: address 0x{address:X} is not file-backed")


def scan_pattern(data: bytes, text: str) -> list[int]:
    pattern = [None if token in {"?", "??"} else int(token, 16) for token in text.split()]
    runs: list[tuple[int, bytes]] = []
    index = 0
    while index < len(pattern):
        if pattern[index] is None:
            index += 1
            continue
        start = index
        values = []
        while index < len(pattern) and pattern[index] is not None:
            values.append(pattern[index])
            index += 1
        runs.append((start, bytes(values)))
    if not runs:
        return []
    anchor_index, anchor = max(runs, key=lambda item: len(item[1]))
    matches: list[int] = []
    cursor = 0
    while (found := data.find(anchor, cursor)) != -1:
        start = found - anchor_index
        cursor = found + 1
        if start < 0 or start + len(pattern) > len(data):
            continue
        if all(value is None or data[start + index] == value for index, value in enumerate(pattern)):
            matches.append(start)
    return matches


def resolve(image: ElfImage, entry: dict) -> tuple[int, int]:
    matches = scan_pattern(image.data, entry["pattern"])
    required = entry.get("occurrences", 1)
    if len(matches) != required:
        raise ValueError(f"expected {required} match, found {len(matches)}")

    file_pos: int | None = matches[0]
    address = image.file_to_va(file_pos)
    operations = entry.get("operations") or ([{"type": "add", "value": entry.get("offset", 0)}])
    for operation in operations:
        kind = operation["type"]
        if kind in {"add", "sub"}:
            amount = int(operation["value"])
            amount = amount if kind == "add" else -amount
            address += amount
            if file_pos is not None:
                file_pos += amount
        elif kind == "rip":
            if file_pos is None:
                raise ValueError("RIP operation requires a file-backed address")
            displacement_offset = int(operation.get("offset", 3))
            instruction_length = int(operation.get("len", 7))
            displacement = struct.unpack_from(image.endian + "i", image.data, file_pos + displacement_offset)[0]
            address = address + displacement_offset + 4 + displacement
            if instruction_length != displacement_offset + 4:
                address += instruction_length - (displacement_offset + 4)
            try:
                file_pos = image.va_to_file(address)
            except ValueError:
                file_pos = None
        elif kind == "slice":
            if file_pos is None:
                raise ValueError("slice operation requires a file-backed address")
            start, end = int(operation["start"]), int(operation["end"])
            raw = image.data[file_pos + start:file_pos + end]
            address = int.from_bytes(raw, "little")
        elif kind == "read":
            file_pos = image.va_to_file(address)
            address = struct.unpack_from(image.endian + "Q", image.data, file_pos)[0]
        else:
            raise ValueError(f"unsupported operation: {kind}")
    return address, len(matches)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--game-dir", type=Path, default=Path(os.environ.get("AXION_CS2_GAME_DIR", DEFAULT_GAME)))
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    images: dict[str, ElfImage] = {}
    result = {"format": 1, "platform": "linux-x86_64-native", "modules": {}, "offsets": {}}
    failures = []

    for name, entry in manifest["signatures"].items():
        module = entry["module"]
        try:
            image = images.setdefault(module, ElfImage(args.game_dir / MODULE_PATHS[module]))
            value, _ = resolve(image, entry)
            result["offsets"][name] = {"module": module, "value": value}
        except (KeyError, OSError, ValueError, struct.error) as error:
            failures.append(f"{name}: {error}")

    for module, image in images.items():
        result["modules"][module] = {
            "sha256": hashlib.sha256(image.data).hexdigest(),
            "path": str(image.path),
        }

    if failures:
        print("native signature validation failed:", file=os.sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=os.sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"resolved {len(result['offsets'])} native Linux signatures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
