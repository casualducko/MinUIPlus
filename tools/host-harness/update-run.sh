#!/bin/sh
# Runs the real Update tool on this computer against a mock GitHub (mock_github.py) with scripted buttons
# and screenshots (script syntax as in run.sh). The staged package lands in the fake card's .userdata/macos/update.
#   RES=/path/to/.system/res MODE=good tools/host-harness/update-run.sh "snap:home,A,snap:found,A,sleep:3000,snap:end"
# Environment: RESULT (ok|failed, with UPDATE_ARGS=--done), RES (first time), MODE (good|badsum|badfile|badpath|none), TOKEN (written to update.cfg), INSTALLED (a version)
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$here/../..
script=${1:?script}
WORK=${WORK:-/tmp/minui-host-harness}
OUT=${OUT:-$WORK/update-shots}
CARD=$WORK/card
mkdir -p "$WORK"
if [ ! -d "$CARD" ]; then "$here/make-card.sh" "$CARD" "${RES:?set RES to a .system/res folder}"; fi
rm -rf "$WORK/platform"
cp -R "$repo/workspace/macos/platform" "$WORK/platform"
python3 "$here/platform-patch.py" "$WORK/platform" "$CARD"
mkdir -p "$CARD/.userdata/shared/.minui" "$CARD/.userdata/macos" "$CARD/.system/macos"
rm -rf "$CARD/.userdata/macos/update"
rm -f "$CARD/.system/macos/fork-version.txt" /tmp/update-ready
[ -n "$INSTALLED" ] && printf '%s\n' "$INSTALLED" > "$CARD/.system/macos/fork-version.txt"
[ -n "$RESULT" ] && { mkdir -p "$CARD/.userdata/macos/update"; echo "$RESULT" > "$CARD/.userdata/macos/update/result.txt"; }
rm -f "$WORK/mock-github.log"
python3 "$here/mock_github.py" "$WORK" "${MODE:-good}" > "$WORK/mock-github.log" 2>&1 &
mock=$!
port=
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
	port=$(sed -n 's/^PORT //p' "$WORK/mock-github.log" | head -1)
	[ -n "$port" ] && break
	sleep 0.25
done
printf 'repo=o/r\n%s' "${TOKEN:+token=$TOKEN
}" > "$CARD/.userdata/macos/update.cfg"
cd "$repo/workspace/all/update"
clang update.c update_core.c ../clock/clocknet.c ../common/scaler.c ../common/utils.c ../common/api.c "$WORK/platform/platform.c" \
	-o "$WORK/update" -I. -I../clock -I../common -I"$WORK/platform" -I/opt/homebrew/include -L/opt/homebrew/lib \
	-DPLATFORM=\"macos\" -DUSE_SDL2 -Wno-builtin-macro-redefined -U__ARM_ARCH -D__ARM_ARCH=4 \
	-O1 -g -std=gnu99 -fno-common -Wall -Wno-unused-function -Wno-tautological-constant-out-of-range-compare \
	-Wno-asm-operand-widths -Wno-deprecated-declarations \
	-lSDL2 -lSDL2_image -lSDL2_ttf -lpthread -lm -lz 2>&1 | grep -E "error|update.c.*warning|update_core.c.*warning" || true
rm -rf "$OUT"; mkdir -p "$OUT"
cd "$WORK"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HP_OUT="$OUT" HP_SCRIPT="$script" UPDATE_API="http://127.0.0.1:$port" \
	perl -e 'alarm 90; exec @ARGV' ./update ${UPDATE_ARGS} > "$OUT/log.txt" 2> "$OUT/err.txt" && status=0 || status=$?
kill $mock 2>/dev/null || true
for f in "$OUT"/*.bmp; do [ -f "$f" ] || continue; sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null 2>&1 && rm "$f"; done
echo "exit status $status"; ls "$OUT" | grep png || true
echo "ready marker: $(cat /tmp/update-ready 2>/dev/null || echo none)"
ls "$CARD/.userdata/macos/update/stage" 2>/dev/null || true
