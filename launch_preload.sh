#!/bin/bash
set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LIBRARY="$SCRIPT_DIR/cs2_axion.so"
STARTUP_LOG="/tmp/cs2_preload_startup.log"

if [ ! -f "$LIBRARY" ]; then
    echo "Missing $LIBRARY; run make first."
    exit 1
fi

export LD_PRELOAD="$LIBRARY${LD_PRELOAD:+:$LD_PRELOAD}"
printf 'launch_preload invoked: %s\nlibrary: %s\ncommand: %s\n' "$(date)" "$LIBRARY" "$*" > "$STARTUP_LOG"
rm -f /tmp/cs2_vulkan_debug.log
exec "$@"
