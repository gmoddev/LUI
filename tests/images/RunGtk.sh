#!/usr/bin/env bash
set -euo pipefail
Host=$(realpath "$1")
Workloads=$(realpath "$2")
Output=$(realpath -m "$3")
shift 3
Fixture=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$Output"
cc -O2 -fPIC -shared $(pkg-config --cflags gtk4) "$Fixture/GtkDecodeProbe.c" -o "$Output/GtkDecodeProbe.so" -ldl -pthread
if [ "$#" -eq 0 ]; then set -- Static16 Static32 Static64 Static128 Static256 Static1024 Explorer Thumbnails ClippedAtlas Atlas; fi
Failed=0
for Case in "$@"; do
    Exit=0
    XDG_CACHE_HOME="$Output/cache" GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none GTK_USE_PORTAL=0 G_DEBUG=fatal-criticals \
        timeout 65 xvfb-run -a dbus-run-session -- env LUI_IMAGE_DIAGNOSTICS=1 LD_PRELOAD="$Output/GtkDecodeProbe.so" \
        "$Host" --project "$Workloads/$Case/lui.json" >"$Output/$Case.log" 2>&1 || Exit=$?
    if [ "$Exit" -eq 0 ] && ! grep -qE '\[LUI:ImageBench\] (Done |Skipped Atlas )' "$Output/$Case.log"; then Exit=1; fi
    if grep -q '\[LUI:ImageBench\] Workers passed=false' "$Output/$Case.log"; then Exit=1; fi
    if [ "$Exit" -ne 0 ]; then Failed=1; fi
    printf '[LUI:ImageBench] %s status=%s\n' "$Case" "$Exit" | tee -a "$Output/Statuses.log"
done
exit "$Failed"
