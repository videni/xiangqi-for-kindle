#!/bin/sh
set -eu
PACKAGE_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PAYLOAD="$PACKAGE_ROOT/payload"
APP="$PAYLOAD/bin/armhf/exact-chinesechess"
if pgrep -f "$APP" >/dev/null 2>&1; then
    exit 0
fi
EXACT_CHINESECHESS_DATA_DIR="${EXACT_CHINESECHESS_DATA_DIR:-/mnt/us/exact-chinesechess}"
EXACT_CHINESECHESS_PIKAFISH="$PAYLOAD/bin/armhf/pikafish"
DISPLAY="${DISPLAY:-:0}"
KINDLE_SHOW_TOPBAR="${KINDLE_SHOW_TOPBAR:-1}"
GDK_NATIVE_WINDOWS=1
export EXACT_CHINESECHESS_DATA_DIR EXACT_CHINESECHESS_PIKAFISH DISPLAY KINDLE_SHOW_TOPBAR GDK_NATIVE_WINDOWS
mkdir -p "$EXACT_CHINESECHESS_DATA_DIR"
PNG_LOADER="$PAYLOAD/lib/armhf/gdk-pixbuf-loaders/libpixbufloader-png.so"
if [ -f "$PNG_LOADER" ]; then
    GDK_PIXBUF_MODULE_FILE="$EXACT_CHINESECHESS_DATA_DIR/loaders.cache"
    printf '"%s"\n"png" 5 "gdk-pixbuf" "PNG" "LGPL"\n"image/png" ""\n"png" ""\n"\\211PNG\\r\\n\\032\\n" "" 100\n\n' \
        "$PNG_LOADER" > "$GDK_PIXBUF_MODULE_FILE"
    export GDK_PIXBUF_MODULE_FILE
fi
cd "$PAYLOAD"
LOADER="$PAYLOAD/lib/armhf/ld-linux-armhf.so.3"
LIBRARIES="$PAYLOAD/lib/armhf:$PAYLOAD/bin/armhf"
if [ -x "$LOADER" ]; then
    exec "$LOADER" --library-path "$LIBRARIES" "$APP" >> "$EXACT_CHINESECHESS_DATA_DIR/launch.log" 2>&1
fi
LD_LIBRARY_PATH="$LIBRARIES${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH
exec "$APP" >> "$EXACT_CHINESECHESS_DATA_DIR/launch.log" 2>&1
