#!/bin/sh
# Runs the real Wi-Fi tool on this computer with fake Wi-Fi commands (tools/host-harness/mock-wifi),
# scripted buttons and screenshots (script syntax as in run.sh; keys U D L R A B X Y S M, snap:name, wait).
#   RES=/path/to/.system/res tools/host-harness/wifi-run.sh "snap:main,D,A,wait,wait,wait,wait,snap:nearby,B"
# Environment: RES (first time), WORK, OUT, KEEP_STATE=1 to keep the fake radio's state from the last run,
#              SAVED (initial wifi-networks.txt contents), MOCK_CONNECTED/MOCK_KEY (start connected to that network), WIFI_TXT (initial ./wifi.txt contents)
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$here/../..
script=${1:?script}
WORK=${WORK:-/tmp/minui-host-harness}
OUT=${OUT:-$WORK/wifi-shots}
CARD=$WORK/card
mkdir -p "$WORK"
if [ ! -d "$CARD" ]; then "$here/make-card.sh" "$CARD" "${RES:?set RES to a .system/res folder}"; fi
rm -rf "$WORK/platform"; export PADDING=${PADDING:-20}
cp -R "$repo/workspace/macos/platform" "$WORK/platform"
python3 "$here/platform-patch.py" "$WORK/platform" "$CARD"
mkdir -p "$CARD/.userdata/macos"
rm -f "$CARD/.userdata/macos/wifi-networks.txt"
[ -n "$SAVED" ] && printf '%s' "$SAVED" > "$CARD/.userdata/macos/wifi-networks.txt"
MOCK=$WORK/wifi-mock
[ -n "$KEEP_STATE" ] || { rm -rf "$MOCK"; mkdir -p "$MOCK"; }
mkdir -p "$MOCK"
if [ -n "$MOCK_CONNECTED" ]; then # start the fake radio already connected to this network
	mkdir -p "$MOCK/net/wlan0"
	printf '%s' "$MOCK_CONNECTED" > "$MOCK/connected"; printf '%s' "$MOCK_CONNECTED" > "$MOCK/s_wifi.ssid"; printf '%s' "${MOCK_KEY:-}" > "$MOCK/s_wifi.key"
	printf '0x1003\n' > "$MOCK/net/wlan0/flags"; printf 'up\n' > "$MOCK/net/wlan0/operstate"
fi
RUNDIR=$WORK/wifi-run; rm -rf "$RUNDIR"; mkdir -p "$RUNDIR"
[ -n "$WIFI_TXT" ] && printf '%s' "$WIFI_TXT" > "$RUNDIR/wifi.txt"
cd "$repo/workspace/all/wifi"
clang wifi.c wifi_core.c ../common/scaler.c ../common/utils.c ../common/api.c "$WORK/platform/platform.c" \
	-o "$WORK/wifi" -I. -I../common -I"$WORK/platform" -I/opt/homebrew/include -L/opt/homebrew/lib \
	-DPLATFORM=\"macos\" -DUSE_SDL2 -Wno-builtin-macro-redefined -U__ARM_ARCH -D__ARM_ARCH=4 \
	-O1 -g -std=gnu99 -fno-common -Wall -Wno-unused-function -Wno-tautological-constant-out-of-range-compare \
	-Wno-asm-operand-widths -Wno-deprecated-declarations \
	-lSDL2 -lSDL2_image -lSDL2_ttf -lpthread -lm -lz 2>&1 | grep -E "error|wifi(_core)?\.c.*warning" || true
rm -rf "$OUT"; mkdir -p "$OUT"
cd "$RUNDIR"
PATH="$here/mock-wifi/bin:$PATH" WIFI_MOCK_DIR="$MOCK" WIFI_NET_DIR="$MOCK/net" \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HP_OUT="$OUT" HP_SCRIPT="$script" \
	perl -e 'alarm 120; exec @ARGV' "$WORK/wifi" > "$OUT/log.txt" 2> "$OUT/err.txt" && status=0 || status=$?
for f in "$OUT"/*.bmp; do [ -f "$f" ] || continue; sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null 2>&1 && rm "$f"; done
echo "exit status $status"; ls "$OUT" | grep png || true
echo "--- saved networks file:"; cat "$CARD/.userdata/macos/wifi-networks.txt" 2>/dev/null || echo "(none)"
echo "--- fake settings:"; for k in wifi.ssid wifi.key; do printf '%s=' $k; cat "$MOCK/s_$k" 2>/dev/null; echo; done
echo "--- fake connected to: $(cat "$MOCK/connected" 2>/dev/null)"
