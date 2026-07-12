#!/bin/bash
set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if [ ! -x "$SCRIPT_DIR/axion_loader" ]; then
    make -C "$SCRIPT_DIR" loader
fi

exec "$SCRIPT_DIR/axion_loader"
