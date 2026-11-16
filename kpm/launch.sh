#!/bin/sh
set -eu
PACKAGE_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PAYLOAD="$PACKAGE_ROOT/payload"
APP="$PAYLOAD/bin/armhf/exact-chinesechess"
EXACT_CHINESECHESS_DATA_DIR="${EXACT_CHINESECHESS_DATA_DIR:-/mnt/us/exact-chinesechess}"
EXACT_CHINESECHESS_PIKAFISH="$PAYLOAD/bin/armhf/pikafish"
DISPLAY="${DISPLAY:-:0}"
GDK_NATIVE_WINDOWS=1
export EXACT_CHINESECHESS_DATA_DIR EXACT_CHINESECHESS_PIKAFISH DISPLAY GDK_NATIVE_WINDOWS
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
EXISTING_PID="$(pgrep -f "$APP\$" | head -n 1 || true)"
if [ -n "$EXISTING_PID" ]; then
    ACTIVATE_ATTEMPTS=0
    while kill -0 "$EXISTING_PID" 2>/dev/null; do
        if [ -x "$LOADER" ]; then
            if "$LOADER" --library-path "$LIBRARIES" "$APP" --activate; then break; fi
        else
            if LD_LIBRARY_PATH="$LIBRARIES${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$APP" --activate; then break; fi
        fi
        ACTIVATE_ATTEMPTS=$((ACTIVATE_ATTEMPTS + 1))
        if [ "$ACTIVATE_ATTEMPTS" -ge 10 ]; then
            echo "The running game window could not be activated" >&2
            exit 1
        fi
        sleep 1
    done
    if kill -0 "$EXISTING_PID" 2>/dev/null; then
        # Keep the scriptlet active until its already-running game exits.
        while kill -0 "$EXISTING_PID" 2>/dev/null; do sleep 1; done
        exit 0
    fi
fi
if [ -x "$LOADER" ]; then
    exec "$LOADER" --library-path "$LIBRARIES" "$APP" >> "$EXACT_CHINESECHESS_DATA_DIR/launch.log" 2>&1
fi
LD_LIBRARY_PATH="$LIBRARIES${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH
exec "$APP" >> "$EXACT_CHINESECHESS_DATA_DIR/launch.log" 2>&1
