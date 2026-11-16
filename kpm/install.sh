#!/bin/sh
set -eu
PACKAGE_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
DOCUMENTS="${EXACT_CHINESECHESS_DOCUMENTS_DIR:-/mnt/us/documents}"
DATA_DIR="${EXACT_CHINESECHESS_DATA_DIR:-/mnt/us/exact-chinesechess}"
ENTRY="$DOCUMENTS/xiangqi.sh"
mkdir -p "$DOCUMENTS" "$DATA_DIR"
if [ -e "$ENTRY" ] && ! cmp -s "$ENTRY" "$PACKAGE_ROOT/scriptlets/xiangqi.sh"; then
    echo "Cannot replace existing script: $ENTRY" >&2
    exit 1
fi
cp "$PACKAGE_ROOT/scriptlets/xiangqi.sh" "$ENTRY"
chmod 755 "$ENTRY"
