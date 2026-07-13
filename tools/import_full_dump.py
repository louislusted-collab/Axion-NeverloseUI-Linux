#!/usr/bin/env python3
"""Convert cs2-dumper's Linux offsets.json to Axion's runtime cache format."""

import argparse
import json
from pathlib import Path


ALIASES = {
    "dwViewMatrix": "dwViewMatrixNative",
    "dwLocalPlayerController": "dwLocalPlayerControllerNative",
    "dwViewAngles": "dwViewAnglesNative",
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
        "interfaces": {},
        "buttons": {},
        "schema_offsets": {},
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

    dump_directory = args.input.parent
    interfaces_path = dump_directory / "interfaces.json"
    if interfaces_path.is_file():
        interfaces = json.loads(interfaces_path.read_text(encoding="utf-8"))
        for module, entries in interfaces.items():
            if not isinstance(entries, dict):
                continue
            for name, value in entries.items():
                if isinstance(name, str) and isinstance(value, int):
                    resolved["interfaces"][f"{module}!{name}"] = {
                        "module": module,
                        "value": value,
                    }

    buttons_path = dump_directory / "buttons.json"
    if buttons_path.is_file():
        buttons = json.loads(buttons_path.read_text(encoding="utf-8"))
        for module, entries in buttons.items():
            if not isinstance(entries, dict):
                continue
            for name, value in entries.items():
                if isinstance(name, str) and isinstance(value, int):
                    resolved["buttons"][f"{module}!{name}"] = {
                        "module": module,
                        "value": value,
                    }

    complete = {
        "format": 1,
        "platform": "linux-x86_64-native",
        "description": "Every signature, interface, input button and schema field emitted by the full dumper.",
        "artifacts": {},
    }
    for artifact in sorted(dump_directory.glob("*.json")):
        if artifact.name == "everything.json":
            continue
        payload = json.loads(artifact.read_text(encoding="utf-8"))
        complete["artifacts"][artifact.name] = payload
        if not artifact.name.startswith("lib") or not isinstance(payload, dict):
            continue
        for module, module_data in payload.items():
            if not isinstance(module_data, dict):
                continue
            classes = module_data.get("classes", {})
            if not isinstance(classes, dict):
                continue
            for class_name, class_data in classes.items():
                fields = class_data.get("fields", {}) if isinstance(class_data, dict) else {}
                if not isinstance(fields, dict):
                    continue
                for field_name, value in fields.items():
                    if isinstance(field_name, str) and isinstance(value, int):
                        resolved["schema_offsets"][f"{module}!{class_name}->{field_name}"] = value

    required = ("dwViewMatrixNative", "dwLocalPlayerControllerNative")
    missing = [name for name in required if name not in resolved["offsets"]]
    if missing:
        raise SystemExit("full dump is missing required offsets: " + ", ".join(missing))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(json.dumps(resolved, indent=2) + "\n", encoding="utf-8")
    temporary.replace(args.output)
    complete_path = dump_directory / "everything.json"
    complete_temporary = complete_path.with_suffix(".json.tmp")
    complete_temporary.write_text(json.dumps(complete, indent=2) + "\n", encoding="utf-8")
    complete_temporary.replace(complete_path)
    print(
        "Imported "
        f"{len(resolved['offsets'])} signatures, "
        f"{len(resolved['interfaces'])} interfaces, "
        f"{len(resolved['buttons'])} buttons and "
        f"{len(resolved['schema_offsets'])} schema fields into Axion."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
