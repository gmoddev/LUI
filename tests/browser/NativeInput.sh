#!/usr/bin/env bash
# Invoke inside an isolated Xvfb + D-Bus session owned by this qualification run.
set -euo pipefail
cd "$(dirname "$0")/../.."
Build=$(realpath "$1")
test -n "${DISPLAY:-}"
test -n "${DBUS_SESSION_BUS_ADDRESS:-}"
for Tool in xfwm4 ibus-daemon ibus xdotool; do command -v "$Tool" >/dev/null; done
Run=$(mktemp -d "$Build/NativeInput.XXXXXX")
export LUI_ORCA_EVIDENCE="$Run/Orca.jsonl" LUI_ORCA_PREFS="$Run/OrcaPrefs"
mkdir "$LUI_ORCA_PREFS"
export LANG=C.UTF-8 LC_ALL=C.UTF-8 GTK_IM_MODULE=ibus QT_IM_MODULE=ibus XMODIFIERS=@im=ibus
unset IBUS_USE_PORTAL
export LUI_BROWSER_SOFTWARE=1 GDK_BACKEND=x11 LIBGL_ALWAYS_SOFTWARE=1 MOZ_ENABLE_WAYLAND=0 WEBKIT_DISABLE_COMPOSITING_MODE=1
unset WAYLAND_DISPLAY
xfwm4 --replace --compositor=off --sm-client-disable >"$Run/WindowManager.log" 2>&1 &
WindowManager=$!
ibus-daemon --xim --replace --panel=disable >"$Run/IBus.log" 2>&1 &
IBus=$!
trap 'kill "$IBus" "$WindowManager" 2>/dev/null || true; wait "$IBus" "$WindowManager" 2>/dev/null || true' EXIT
for Attempt in $(seq 1 100); do
    if ibus list-engine 2>/dev/null | grep libpinyin >/dev/null; then break; fi
    sleep 0.1
done
ibus list-engine | grep libpinyin >/dev/null
export IBUS_ADDRESS
IBUS_ADDRESS=$(ibus address)
# Separate input-provider and assistive-technology sessions so each has its own
# browser context and a precise evidence scope.
LUI_BROWSER_INPUT_ONLY=1 node tests/browser/NativeInput.cjs "$Build"
mkdir "$Run/SpeechConfig" "$Run/SpeechLogs"
cat > "$Run/SpeechConfig/speechd.conf" <<EOF
LogLevel 1
LogDir "$Run/SpeechLogs"
DefaultModule dummy
AddModule "dummy" "sd_dummy" ""
AudioOutputMethod "alsa"
AudioALSADevice "null"
EOF
# A real isolated dispatcher with its dummy sink tests Orca speech submissions;
# audio quality and physical listening are deliberately outside this session.
export SPEECHD_ADDRESS="unix_socket:$Run/speech.sock"
speech-dispatcher -s -c unix_socket -C "$Run/SpeechConfig" -S "$Run/speech.sock" -P "$Run/speech.pid" -t 0 -l 1 -L "$Run/SpeechLogs" >"$Run/Speech.log" 2>&1 &
Speech=$!
trap 'kill "$IBus" "$Speech" "$WindowManager" 2>/dev/null || true; wait "$IBus" "$Speech" "$WindowManager" 2>/dev/null || true' EXIT
for Attempt in $(seq 1 100); do
    if test -S "$Run/speech.sock"; then break; fi
    if ! kill -0 "$Speech" 2>/dev/null; then echo '[LUI:BrowserTest] Speech dispatcher did not start'; exit 1; fi
    sleep 0.1
done
test -S "$Run/speech.sock"
/usr/bin/python3 tests/browser/OrcaProbe.py >"$Run/Orca.log" 2>&1 &
Orca=$!
StopOrca() {
    # Orca disconnects synchronously from Speech Dispatcher. Keep that server
    # alive until the reader exits; simultaneous shutdown can block its CANCEL.
    kill "$Orca" 2>/dev/null || true
    for Attempt in $(seq 1 30); do
        if ! kill -0 "$Orca" 2>/dev/null; then wait "$Orca" 2>/dev/null || true; return 0; fi
        sleep 0.1
    done
    kill -KILL "$Orca" 2>/dev/null || true
    wait "$Orca" 2>/dev/null || true
    return 1
}
trap 'StopOrca || true; kill "$IBus" "$Speech" "$WindowManager" 2>/dev/null || true; wait "$IBus" "$Speech" "$WindowManager" 2>/dev/null || true' EXIT
for Attempt in $(seq 1 100); do
    if test -f "$LUI_ORCA_EVIDENCE" && grep -q '"Ready": true' "$LUI_ORCA_EVIDENCE" && ibus list-engine 2>/dev/null | grep libpinyin >/dev/null; then break; fi
    if ! kill -0 "$Orca" 2>/dev/null; then echo '[LUI:BrowserTest] Orca did not start'; tail -n 12 "$Run/Orca.log"; exit 1; fi
    sleep 0.1
done
if ! test -f "$LUI_ORCA_EVIDENCE" || ! grep -q '"Ready": true' "$LUI_ORCA_EVIDENCE"; then echo '[LUI:BrowserTest] Orca readiness missing'; tail -n 12 "$Run/Orca.log"; exit 1; fi
ibus list-engine | grep libpinyin >/dev/null
LUI_BROWSER_AT_ONLY=1 node tests/browser/NativeInput.cjs "$Build"
if ! StopOrca; then echo '[LUI:BrowserTest] Orca did not retire within its cleanup bound'; exit 1; fi
if grep -E 'Traceback|orca.py.*in timeout|ERROR: No speech server' "$Run/Orca.log"; then
    echo '[LUI:BrowserTest] Orca session or retirement diagnostic failed'; exit 1
fi
echo '[LUI:BrowserTest] Orca retired before dispatcher shutdown without session errors'
