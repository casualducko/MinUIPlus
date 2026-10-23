#!/bin/sh
# usage: RES=/path/to/.system/res run.sh "snap:root,D,A,snap:x"
# See README.md. Environment: RES (required the first time), WORK, OUT.
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$here/../..
script=${1:?script, eg. snap:root,D,A,snap:next}
WORK=${WORK:-/tmp/minui-host-harness}
OUT=${OUT:-$WORK/shots}
CARD=$WORK/card

mkdir -p "$WORK"
if [ ! -d "$CARD" ]; then "$here/make-card.sh" "$CARD" "${RES:?set RES to a .system/res folder}"; fi

# test platform: a patched copy of the macos one
rm -rf "$WORK/platform"
cp -R "$repo/workspace/macos/platform" "$WORK/platform"
python3 "$here/platform-patch.py" "$WORK/platform" "$CARD"

# build the real launcher (not sanitized, see README)
cd "$repo/workspace/all/minui"
clang minui.c ../common/scaler.c ../common/utils.c ../common/api.c "$WORK/platform/platform.c" \
	-o "$WORK/minui" -I. -I../common -I"$WORK/platform" -I/opt/homebrew/include -L/opt/homebrew/lib \
	-DPLATFORM=\"macos\" -DUSE_SDL2 -Wno-builtin-macro-redefined -U__ARM_ARCH -D__ARM_ARCH=4 \
	-O1 -g -std=gnu99 -fno-common -Wno-tautological-constant-out-of-range-compare \
	-Wno-asm-operand-widths -Wno-deprecated-declarations \
	-lSDL2 -lSDL2_image -lSDL2_ttf -lpthread -lm -lz

# MinUI reopens at the last launched ROM, start clean
rm -rf "$OUT"; mkdir -p "$OUT"
rm -f /tmp/last.txt /tmp/next /tmp/change_disc.txt "$CARD/.userdata/shared/.minui/recent.txt"

cd "$WORK"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HP_OUT="$OUT" HP_SCRIPT="$script" \
	perl -e 'alarm 120; exec @ARGV' ./minui > "$OUT/log.txt" 2> "$OUT/err.txt" && status=0 || status=$?
for f in "$OUT"/*.bmp; do
	[ -f "$f" ] || continue
	sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null 2>&1 && rm "$f"
done
echo "exit status $status; screenshots:"; ls "$OUT" | grep png
