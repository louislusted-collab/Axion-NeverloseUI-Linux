#!/bin/bash
set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LIBRARY="$SCRIPT_DIR/cs2_axion.so"
UPDATER="$SCRIPT_DIR/tools/update_offsets.sh"

if [ ! -f "$LIBRARY" ]; then
    echo "Missing $LIBRARY; run make first."
    exit 1
fi

if [ "${AXION_SKIP_UPDATE:-0}" != "1" ] && [ "${AXION_UPDATE_DONE:-0}" != "1" ] && [ -x "$UPDATER" ]; then
    "$UPDATER" || echo "Full dump update failed; injection will use the last validated offsets." >&2
fi

PID="$(pidof cs2 2>/dev/null || true)"
if [ -z "$PID" ]; then
    echo "CS2 is not running. Start it normally through Steam first."
    exit 1
fi

if [ "$EUID" -ne 0 ]; then
    exec sudo env AXION_UPDATE_DONE=1 "$0" "$@"
fi

rm -f /tmp/cs2_vulkan_debug.log /tmp/cs2_inject_debug.log /tmp/cs2_init_debug.log /tmp/cs2_hook_debug.log /tmp/cs2_esp_debug.log
echo "Injecting $LIBRARY into CS2 pid $PID..."
gdb -n --batch \
    -ex "attach $PID" \
    -ex "call ((void*(*)(const char*,int))dlopen)(\"$LIBRARY\", 1)" \
    -ex "detach"

echo "Injected. The menu opens automatically; Insert toggles it."
echo "Renderer log: /tmp/cs2_vulkan_debug.log"
