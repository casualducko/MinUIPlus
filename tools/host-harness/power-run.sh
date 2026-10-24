#!/bin/sh
# Runs the real Sleep & Power tool on this computer with scripted buttons and screenshots
# (script syntax as in run.sh; keys: U D L R A B, snap:name, wait).
#   RES=/path/to/.system/res tools/host-harness/power-run.sh "snap:first,D,R,snap:second,B"
# Environment: RES (first time), WORK, OUT, SLEEP_FILE / POWEROFF_FILE (initial file contents, optional)
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$here/../..
script=${1:?script}
WORK=${WORK:-/tmp/minui-host-harness}
OUT=${OUT:-$WORK/power-shots}
CARD=$WORK/card
mkdir -p "$WORK"
if [ ! -d "$CARD" ]; then "$here/make-card.sh" "$CARD" "${RES:?set RES to a .system/res folder}"; fi
rm -rf "$WORK/platform"
cp -R "$repo/workspace/macos/platform" "$WORK/platform"
python3 "$here/platform-patch.py" "$WORK/platform" "$CARD"
mkdir -p "$CARD/.userdata/macos"
rm -f "$CARD/.userdata/macos/sleep-timeout" "$CARD/.userdata/macos/poweroff-timeout"
[ -n "$SLEEP_FILE" ] && printf '%s' "$SLEEP_FILE" > "$CARD/.userdata/macos/sleep-timeout"
[ -n "$POWEROFF_FILE" ] && printf '%s' "$POWEROFF_FILE" > "$CARD/.userdata/macos/poweroff-timeout"
cd "$repo/workspace/all/power"
clang power.c ../common/scaler.c ../common/utils.c ../common/api.c "$WORK/platform/platform.c" \
	-o "$WORK/power" -I. -I../common -I"$WORK/platform" -I/opt/homebrew/include -L/opt/homebrew/lib \
	-DPLATFORM=\"macos\" -DUSE_SDL2 -Wno-builtin-macro-redefined -U__ARM_ARCH -D__ARM_ARCH=4 \
	-O1 -g -std=gnu99 -fno-common -Wall -Wno-unused-function -Wno-tautological-constant-out-of-range-compare \
	-Wno-asm-operand-widths -Wno-deprecated-declarations \
	-lSDL2 -lSDL2_image -lSDL2_ttf -lpthread -lm -lz 2>&1 | grep -E "error|power.c.*warning" || true
rm -rf "$OUT"; mkdir -p "$OUT"
cd "$WORK"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HP_OUT="$OUT" HP_SCRIPT="$script" \
	perl -e 'alarm 60; exec @ARGV' ./power > "$OUT/log.txt" 2> "$OUT/err.txt" && status=0 || status=$?
for f in "$OUT"/*.bmp; do [ -f "$f" ] || continue; sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null 2>&1 && rm "$f"; done
echo "exit status $status"; ls "$OUT" | grep png || true
for f in sleep-timeout poweroff-timeout; do printf '%s: ' $f; cat "$CARD/.userdata/macos/$f" 2>/dev/null || echo "(none)"; done
