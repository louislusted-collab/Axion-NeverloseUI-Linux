#!/bin/bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE="$ROOT/tools/cs2-dumper"
CACHE="$ROOT/.axion-cache"
TARGET="$CACHE/cs2-dumper-target"
RUNTIME="$CACHE/cs2-dumper-runtime"
OUTPUT="$CACHE/cs2-dumper-output"
UPSTREAM_CONFIG="$CACHE/cs2-dumper-config.json"
CONFIG_URL="${AXION_DUMPER_CONFIG_URL:-https://raw.githubusercontent.com/catpetter1999/cs2-dumper/linux/config.json}"

PID="$(pgrep -n -x cs2 2>/dev/null || true)"
if [ -z "$PID" ]; then
    if [ -s "$OUTPUT/offsets.json" ]; then
        "$ROOT/tools/import_full_dump.py" "$OUTPUT/offsets.json" "$CACHE/linux-offsets.resolved.json"
        echo "Full dumper: kept the last live dump; start native CS2 to refresh it."
    else
        echo "Full dumper: ready; start native CS2, then press Update."
    fi
    exit 0
fi

if [ ! -f "$SOURCE/Cargo.toml" ]; then
    git -C "$ROOT" submodule update --init --depth 1 tools/cs2-dumper
fi
if ! command -v cargo >/dev/null 2>&1; then
    echo "Full dumper: Rust/cargo is not installed." >&2
    exit 1
fi

mkdir -p "$CACHE" "$RUNTIME"
if command -v curl >/dev/null 2>&1; then
    curl --fail --silent --show-error --location --connect-timeout 3 --max-time 10 \
        "$CONFIG_URL" -o "$UPSTREAM_CONFIG.download" || true
fi
if [ -s "$UPSTREAM_CONFIG.download" ] && python3 -m json.tool "$UPSTREAM_CONFIG.download" >/dev/null 2>&1; then
    mv -f "$UPSTREAM_CONFIG.download" "$UPSTREAM_CONFIG"
else
    rm -f "$UPSTREAM_CONFIG.download"
fi
if [ ! -f "$UPSTREAM_CONFIG" ]; then
    cp -f "$SOURCE/config.json" "$UPSTREAM_CONFIG"
fi

"$ROOT/tools/prepare_full_dumper_config.py" \
    "$UPSTREAM_CONFIG" "/proc/$PID/maps" "$RUNTIME/config.json"

if [ ! -x "$TARGET/release/cs2-dumper" ]; then
    echo "Full dumper: compiling once (Rust release build)…"
    CARGO_TARGET_DIR="$TARGET" cargo build --release --manifest-path "$SOURCE/Cargo.toml"
fi

rm -rf "$OUTPUT.new"
mkdir -p "$OUTPUT.new"
echo "Full dumper: reading live offsets, interfaces, buttons, and schemas…"
(
    cd "$RUNTIME"
    "$TARGET/release/cs2-dumper" -f json -o "$OUTPUT.new"
)

test -s "$OUTPUT.new/offsets.json"
rm -rf "$OUTPUT"
mv "$OUTPUT.new" "$OUTPUT"
"$ROOT/tools/import_full_dump.py" "$OUTPUT/offsets.json" "$CACHE/linux-offsets.resolved.json"
echo "Full dump saved to $OUTPUT"
