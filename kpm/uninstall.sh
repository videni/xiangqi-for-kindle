#!/bin/sh
set -eu
PACKAGE_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
DOCUMENTS="${EXACT_CHINESECHESS_DOCUMENTS_DIR:-/mnt/us/documents}"
ENTRY="$DOCUMENTS/xiangqi.sh"
if [ -f "$ENTRY" ] && cmp -s "$ENTRY" "$PACKAGE_ROOT/scriptlets/xiangqi.sh"; then
    rm "$ENTRY"
fi
# 存档独立于软件包，升级和卸载均保留。
