#!/bin/bash
set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Updating is an explicit loader action and can take several minutes. Inject
# must go straight to authorization and use the last validated dump.
exec pkexec env AXION_SKIP_UPDATE=1 "$SCRIPT_DIR/inject.sh"
