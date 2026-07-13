#!/bin/bash
set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
"$SCRIPT_DIR/tools/update_offsets.sh" || \
    echo "Full dump update failed; injection will use the last validated offsets." >&2
exec pkexec env AXION_SKIP_UPDATE=1 "$SCRIPT_DIR/inject.sh"
