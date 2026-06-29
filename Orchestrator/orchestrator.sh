#!/usr/bin/env bash
# Open the preset editor GUI.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
python3 "$SCRIPT_DIR/src/json_editor.py" "$@" &