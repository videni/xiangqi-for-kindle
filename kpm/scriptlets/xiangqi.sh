#!/bin/sh
# Name: 象棋
# Package: exactchinesechess
# DontUseFBInk
DATA_DIR="${EXACT_CHINESECHESS_DATA_DIR:-/mnt/us/exact-chinesechess}"
mkdir -p "$DATA_DIR"
exec /var/local/kmc/bin/kpm launch exactchinesechess > "$DATA_DIR/scriptlet.log" 2>&1
