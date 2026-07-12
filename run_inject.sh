#!/bin/bash
set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
"$SCRIPT_DIR/tools/update_offsets.sh"
exec pkexec env AXION_SKIP_UPDATE=1 "$SCRIPT_DIR/inject.sh"
