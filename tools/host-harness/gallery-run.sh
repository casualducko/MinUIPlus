#!/bin/sh
# Runs the real Screenshot gallery on this computer with scripted buttons and screenshots
# (script syntax as in run.sh; keys: U D L R A B, snap:name, wait).
#   RES=/path/to/.system/res tools/host-harness/power-run.sh "snap:first,D,R,snap:second,B"
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$here/../..
script=${1:?script}
WORK=${WORK:-/tmp/minui-host-harness}
OUT=${OUT:-$WORK/gallery-shots}
CARD=$WORK/card
mkdir -p "$WORK"
if [ ! -d "$CARD" ]; then "$here/make-card.sh" "$CARD" "${RES:?set RES to a .system/res folder}"; fi
rm -rf "$WORK/platform"
cp -R "$repo/workspace/macos/platform" "$WORK/platform"
python3 "$here/platform-patch.py" "$WORK/platform" "$CARD"
mkdir -p "$CARD/.userdata/shared/.minui"
# fake screenshots: three games, a Game Boy shot (small, scaled by whole numbers), a wide one, a square one
rm -rf "$CARD/Screenshots"; mkdir -p "$CARD/Screenshots/GBC" "$CARD/Screenshots/SFC"
python3 - "$CARD" <<'PY'
import sys, os
from PIL import Image, ImageDraw
card = sys.argv[1]
def shot(path, w, h, base):
    im = Image.new("RGB", (w, h), base); d = ImageDraw.Draw(im)
    for i in range(0, w, 16): d.rectangle([i, h//2, i+14, h-1], fill=(110,70,30))
    d.ellipse([w*0.7, h*0.1, w*0.7+h*0.25, h*0.1+h*0.25], fill=(255,240,160))
    d.rectangle([w//2-6, h//2-20, w//2+6, h//2], fill=(210,40,40))
    im.save(path)
shot(card + "/Screenshots/GBC/Super Mario Land 2 DX - 6 Golden Coins 20261024-223105.png", 160, 144, (60,90,170))
shot(card + "/Screenshots/GBC/Super Mario Land 2 DX - 6 Golden Coins 20261024-223105-2.png", 160, 144, (170,90,60))
shot(card + "/Screenshots/SFC/Legend of Zelda, The 20261023-120000.png", 1080, 960, (30,120,60))
shot(card + "/Screenshots/SFC/Mega Man X 20260101-000000.png", 1080, 810, (20,20,90))
open(card + "/Screenshots/SFC/broken 20260202-000000.png", "w").write("not a picture")
PY
cd "$repo/workspace/all/gallery"
clang gallery.c gallery_core.c ../common/scaler.c ../common/utils.c ../common/api.c "$WORK/platform/platform.c" \
	-o "$WORK/gallery" -I. -I../common -I"$WORK/platform" -I/opt/homebrew/include -L/opt/homebrew/lib \
	-DPLATFORM=\"macos\" -DUSE_SDL2 -Wno-builtin-macro-redefined -U__ARM_ARCH -D__ARM_ARCH=4 \
	-O1 -g -std=gnu99 -fno-common -Wall -Wno-unused-function -Wno-tautological-constant-out-of-range-compare \
	-Wno-asm-operand-widths -Wno-deprecated-declarations \
	-lSDL2 -lSDL2_image -lSDL2_ttf -lpthread -lm -lz 2>&1 | grep -E "error|gallery.c.*warning" || true
rm -rf "$OUT"; mkdir -p "$OUT"
cd "$WORK"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HP_OUT="$OUT" HP_SCRIPT="$script" \
	perl -e 'alarm 60; exec @ARGV' ./gallery > "$OUT/log.txt" 2> "$OUT/err.txt" && status=0 || status=$?
for f in "$OUT"/*.bmp; do [ -f "$f" ] || continue; sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null 2>&1 && rm "$f"; done
echo "exit status $status"; ls "$OUT" | grep png || true
