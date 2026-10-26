#!/bin/sh
# Runs the real Pico Search tool on this computer against tools/host-tests/mock_bbs.py,
# with scripted buttons and screenshots (see run.sh for the script syntax; S = START, wait).
#   RES=/path/to/.system/res tools/host-harness/pico-run.sh "wait,A,snap:osk"
# Environment: RES (first time), WORK, OUT, KEEP=1 (keep the card's Roms as the last run left them)
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$here/../..
script=${1:?script}
WORK=${WORK:-/tmp/minui-host-harness}
OUT=${OUT:-$WORK/pico-shots}
CARD=$WORK/card
mkdir -p "$WORK"
if [ ! -d "$CARD" ]; then "$here/make-card.sh" "$CARD" "${RES:?set RES to a .system/res folder}"; fi

rm -rf "$WORK/platform"
cp -R "$repo/workspace/macos/platform" "$WORK/platform"
python3 "$here/platform-patch.py" "$WORK/platform" "$CARD"

# a card with native PICO-8 (its pak and binary folder), so the carts go to "Pico-8 (P8-NATIVE)" and can be played
mkdir -p "$CARD/.system/macos/paks/Emus/P8-NATIVE.pak" "$CARD/Tools/macos/Splore.pak/pico-8" "$CARD/.userdata/macos"
echo '#!/bin/sh' > "$CARD/.system/macos/paks/Emus/P8-NATIVE.pak/launch.sh"
if [ -z "$KEEP" ]; then rm -rf "$CARD/Roms/Pico-8"* "$CARD/.userdata/macos/pico"*; fi
mkdir -p "$CARD/Roms"

python3 "$repo/tools/host-tests/mock_bbs.py" > "$WORK/mock-bbs.log" 2>&1 &
mock=$!
trap 'kill $mock 2>/dev/null' EXIT
port=
for i in $(seq 1 20); do port=$(sed -n 's/^PORT //p' "$WORK/mock-bbs.log" | head -1); [ -n "$port" ] && break; sleep 0.25; done
export PICO_BBS_BASE="http://127.0.0.1:$port"

cd "$repo/workspace/all/pico"
clang pico.c bbs.c ../common/scaler.c ../common/utils.c ../common/api.c "$WORK/platform/platform.c" \
	-o "$WORK/pico" -I. -I../common -I"$WORK/platform" -I/opt/homebrew/include -L/opt/homebrew/lib \
	-DPLATFORM=\"macos\" -DUSE_SDL2 -Wno-builtin-macro-redefined -U__ARM_ARCH -D__ARM_ARCH=4 \
	-O1 -g -std=gnu99 -fno-common -Wall -Wno-unused-function -Wno-tautological-constant-out-of-range-compare \
	-Wno-asm-operand-widths -Wno-deprecated-declarations \
	-lSDL2 -lSDL2_image -lSDL2_ttf -lpthread -lm -lz 2>&1 | grep -E "error|pico.*warning|bbs.*warning" || true

rm -rf "$OUT"; mkdir -p "$OUT"
cd "$WORK"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HP_OUT="$OUT" HP_SCRIPT="$script" \
	perl -e 'alarm 120; exec @ARGV' ./pico > "$OUT/log.txt" 2> "$OUT/err.txt" && status=0 || status=$?
for f in "$OUT"/*.bmp; do [ -f "$f" ] || continue; sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null 2>&1 && rm "$f"; done
echo "exit status $status"; ls "$OUT" | grep png || true
echo "--- card Roms:"; (cd "$CARD" && find Roms -type f | sort)
