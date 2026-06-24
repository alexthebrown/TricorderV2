#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

export TRICORDER_LOCAL=1
export TRICORDER_VIDEO_DIR="${TRICORDER_VIDEO_DIR:-$SCRIPT_DIR/videos}"
export TRICORDER_WINDOW_SIZE="${TRICORDER_WINDOW_SIZE:-720x576}"

mkdir -p "$TRICORDER_VIDEO_DIR"

python3 gpTricorder.py
