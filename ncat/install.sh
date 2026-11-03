#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOURCE="$SCRIPT_DIR/ncat"
DEST_DIR=/usr/local/bin
DEST="$DEST_DIR/ncat"

if [ ! -x "$SOURCE" ]; then
    echo "install.sh: executable not found: $SOURCE" >&2
    exit 1
fi

if [ "$(id -u)" -eq 0 ]; then
    install -m 755 "$SOURCE" "$DEST"
else
    if ! command -v sudo >/dev/null 2>&1; then
        echo "install.sh: install to $DEST requires root; rerun with sudo or as root" >&2
        exit 1
    fi
    sudo install -m 755 "$SOURCE" "$DEST"
fi

echo "Installed ncat to $DEST"
