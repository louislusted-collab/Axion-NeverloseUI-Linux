#!/bin/bash
set -u

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="$ROOT/.axion-cache"
LOCAL="$ROOT/offsets/linux-signatures.json"
ACTIVE="$CACHE/linux-signatures.json"
BACKUP="$CACHE/linux-signatures.last-good.json"
TEMP="$CACHE/linux-signatures.download"
TEMP_RESOLVED="$CACHE/linux-offsets.download.json"
RESOLVED="$CACHE/linux-offsets.resolved.json"
VALIDATOR="$ROOT/tools/validate_offsets.py"
SCANNER="$ROOT/tools/dump_linux_offsets.py"
FULL_DUMPER="$ROOT/tools/run_full_linux_dumper.sh"
REMOTE="${AXION_OFFSETS_URL:-https://raw.githubusercontent.com/louislusted-collab/Axion-NeverloseUI-Linux/main/offsets/linux-signatures.json}"

mkdir -p "$CACHE"

run_full_dumper() {
    if [ -x "$FULL_DUMPER" ]; then
        "$FULL_DUMPER" || echo "Full dumper failed; kept the validated static cache." >&2
    fi
}

trap run_full_dumper EXIT

report_active() {
    if [ -f "$ACTIVE" ]; then
        "$VALIDATOR" "$ACTIVE"
        "$SCANNER" "$ACTIVE" "$RESOLVED"
    fi
}

activate_local() {
    if ! "$VALIDATOR" "$LOCAL" >/dev/null; then
        echo "Bundled Linux signature manifest is invalid." >&2
        return 1
    fi
    if ! "$SCANNER" "$LOCAL" "$TEMP_RESOLVED" >/dev/null; then
        echo "Bundled signatures do not match the installed native CS2 build." >&2
        return 1
    fi
    cp -f "$LOCAL" "$ACTIVE"
    mv -f "$TEMP_RESOLVED" "$RESOLVED"
    echo "Offsets: using bundled native-Linux manifest."
}

if ! command -v curl >/dev/null 2>&1; then
    [ -f "$ACTIVE" ] || activate_local
    echo "Offsets: curl unavailable; kept last-known-good manifest."
    report_active
    exit 0
fi

if ! curl --fail --silent --show-error --location \
    --connect-timeout 3 --max-time 8 "$REMOTE" -o "$TEMP"; then
    rm -f "$TEMP"
    [ -f "$ACTIVE" ] || activate_local
    echo "Offsets: update server unavailable; kept last-known-good manifest."
    report_active
    exit 0
fi

if ! "$VALIDATOR" "$TEMP" >/dev/null; then
    rm -f "$TEMP" "$TEMP_RESOLVED"
    [ -f "$ACTIVE" ] || activate_local
    echo "Offsets: rejected invalid update; kept last-known-good manifest." >&2
    report_active
    exit 0
fi

if ! "$SCANNER" "$TEMP" "$TEMP_RESOLVED" >/dev/null; then
    rm -f "$TEMP" "$TEMP_RESOLVED"
    [ -f "$ACTIVE" ] || activate_local
    echo "Offsets: update does not match installed native CS2; kept last-known-good manifest." >&2
    report_active
    exit 0
fi

if [ -f "$ACTIVE" ] && cmp -s "$TEMP" "$ACTIVE"; then
    rm -f "$TEMP" "$TEMP_RESOLVED"
    echo "Offsets: already current."
    report_active
    exit 0
fi

if [ -f "$ACTIVE" ]; then
    cp -f "$ACTIVE" "$BACKUP"
fi
mv -f "$TEMP" "$ACTIVE"
mv -f "$TEMP_RESOLVED" "$RESOLVED"
echo "Offsets: installed validated native-Linux manifest update."
report_active
