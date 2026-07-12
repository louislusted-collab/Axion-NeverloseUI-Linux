#!/bin/bash
set -u

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="$ROOT/.axion-cache"
LOCAL="$ROOT/offsets/linux-signatures.json"
ACTIVE="$CACHE/linux-signatures.json"
BACKUP="$CACHE/linux-signatures.last-good.json"
TEMP="$CACHE/linux-signatures.download"
VALIDATOR="$ROOT/tools/validate_offsets.py"
REMOTE="${AXION_OFFSETS_URL:-https://raw.githubusercontent.com/louislusted-collab/Axion-NeverloseUI-Linux/main/offsets/linux-signatures.json}"

mkdir -p "$CACHE"

activate_local() {
    if ! "$VALIDATOR" "$LOCAL" >/dev/null; then
        echo "Bundled Linux signature manifest is invalid." >&2
        return 1
    fi
    cp -f "$LOCAL" "$ACTIVE"
    echo "Offsets: using bundled native-Linux manifest."
}

if ! command -v curl >/dev/null 2>&1; then
    [ -f "$ACTIVE" ] || activate_local
    echo "Offsets: curl unavailable; kept last-known-good manifest."
    exit 0
fi

if ! curl --fail --silent --show-error --location \
    --connect-timeout 3 --max-time 8 "$REMOTE" -o "$TEMP"; then
    rm -f "$TEMP"
    [ -f "$ACTIVE" ] || activate_local
    echo "Offsets: update server unavailable; kept last-known-good manifest."
    exit 0
fi

if ! "$VALIDATOR" "$TEMP" >/dev/null; then
    rm -f "$TEMP"
    [ -f "$ACTIVE" ] || activate_local
    echo "Offsets: rejected invalid update; kept last-known-good manifest." >&2
    exit 0
fi

if [ -f "$ACTIVE" ] && cmp -s "$TEMP" "$ACTIVE"; then
    rm -f "$TEMP"
    echo "Offsets: already current."
    exit 0
fi

if [ -f "$ACTIVE" ]; then
    cp -f "$ACTIVE" "$BACKUP"
fi
mv -f "$TEMP" "$ACTIVE"
echo "Offsets: installed validated native-Linux manifest update."
