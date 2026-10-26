#!/bin/sh
# Runs the real Switcher tool on this computer against a fake card with some recent games and save-state
# pictures, with scripted buttons and screenshots (script syntax as in run.sh; S = START, SEL = SELECT).
#   RES=/path/to/.system/res tools/host-harness/switcher-run.sh "snap:first,R,snap:second,A"
# After the run: the card's .userdata/shared/.minui/recent.txt and /tmp/switch-to.txt show what happened.
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$here/../..
script=${1:?script}
WORK=${WORK:-/tmp/minui-host-harness}
OUT=${OUT:-$WORK/switcher-shots}
CARD=$WORK/card
mkdir -p "$WORK"
if [ ! -d "$CARD" ]; then "$here/make-card.sh" "$CARD" "${RES:?set RES to a .system/res folder}"; fi
rm -rf "$WORK/platform"
cp -R "$repo/workspace/macos/platform" "$WORK/platform"
python3 "$here/platform-patch.py" "$WORK/platform" "$CARD"
rm -f /tmp/switch-to.txt

# emulator paks: the ones that run minarch have save states, the native PICO-8 does not (the tool skips it)
for tag in MGBA SFC PS FC; do
	mkdir -p "$CARD/.system/macos/paks/Emus/$tag.pak"
	printf '#!/bin/sh\nminarch.elf core "$1"\n' > "$CARD/.system/macos/paks/Emus/$tag.pak/launch.sh"
done
mkdir -p "$CARD/.system/macos/paks/Emus/P8-NATIVE.pak"
printf '#!/bin/sh\n./pico8_64 -run "$1"\n' > "$CARD/.system/macos/paks/Emus/P8-NATIVE.pak/launch.sh"
python3 - "$CARD" <<'PY'
import os, sys, struct
from PIL import Image, ImageDraw
card = sys.argv[1]
games = [  # folder, file, preview size, slot (None: no state), colour
 ("GBA (MGBA)", "Pokemon ROWE (v2.02).gba", (240,160), 2, (40,120,60)),
 ("SNES (SFC)", "Legend of Zelda, The (USA).sfc", (256,224), 0, (60,80,150)),
 ("Playstation (PS)", "Castlevania - Symphony of the Night.chd", (320,240), 4, (110,40,60)),
 ("NES (FC)", "Mega Man (USA).nes", (256,240), None, (0,0,0)),
 ("Pico-8 (P8-NATIVE)", "Celeste Classic.p8.png", (128,128), None, (0,0,0)),
]
shared = os.path.join(card, ".userdata", "shared", ".minui")
os.makedirs(shared, exist_ok=True)
lines = []
for folder, file, size, slot, col in games:
    d = os.path.join(card, "Roms", folder); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, file), "w").write("x")
    lines.append("/Roms/%s/%s" % (folder, file))
    if slot is None: continue
    tag = folder[folder.rindex("(")+1:-1]
    sd = os.path.join(shared, tag); os.makedirs(sd, exist_ok=True)
    open(os.path.join(sd, file + ".txt"), "w").write("%d\n" % slot)
    im = Image.new("RGB", size, col); dr = ImageDraw.Draw(im)
    for i in range(0, size[0], 16): dr.line([(i,0),(size[0]-1-i,size[1]-1)], fill=(255,255,255))
    dr.rectangle([4,4,size[0]-5,size[1]-5], outline=(255,255,0))
    dr.text((10,10), file[:24], fill=(255,255,255))
    im.save(os.path.join(sd, "%s.%d.bmp" % (file, slot)))
    os.utime(os.path.join(sd, "%s.%d.bmp" % (file, slot)), (os.path.getatime(sd), os.path.getmtime(sd) - 3600*5*(slot+1)))
# the GBA game was also auto-saved on quit a minute ago (slot 9), newer than its manual slot: it resumes from that one
sd = os.path.join(shared, "MGBA"); os.makedirs(sd, exist_ok=True)
im = Image.new("RGB", (240,160), (200,120,20)); dr = ImageDraw.Draw(im); dr.text((10,10), "auto-save (slot 9)", fill=(255,255,255)); dr.rectangle([4,4,235,155], outline=(255,255,255))
im.save(os.path.join(sd, "Pokemon ROWE (v2.02).gba.9.bmp"))
open(os.path.join(shared, "recent.txt"), "w").write("\n".join(lines + ["/Roms/Gone (GB)/Missing.gb"]) + "\n")
open(os.path.join(shared, "playtime.txt"), "w").write("15420\t12\t1790000000\t/Roms/GBA (MGBA)/Pokemon ROWE (v2.02).gba\n600\t3\t1790000000\t/Roms/SNES (SFC)/Legend of Zelda, The (USA).sfc\n")
PY

cd "$repo/workspace/all/switcher"
clang switcher.c switcher_core.c ../common/scaler.c ../common/utils.c ../common/api.c "$WORK/platform/platform.c" \
	-o "$WORK/switcher" -I. -I../common -I"$WORK/platform" -I/opt/homebrew/include -L/opt/homebrew/lib \
	-DPLATFORM=\"macos\" -DUSE_SDL2 -Wno-builtin-macro-redefined -U__ARM_ARCH -D__ARM_ARCH=4 \
	-O1 -g -std=gnu99 -fno-common -Wall -Wno-unused-function -Wno-tautological-constant-out-of-range-compare \
	-Wno-asm-operand-widths -Wno-deprecated-declarations \
	-lSDL2 -lSDL2_image -lSDL2_ttf -lpthread -lm -lz 2>&1 | grep -E "error|switcher.*warning" || true
rm -rf "$OUT"; mkdir -p "$OUT"
cd "$WORK"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HP_OUT="$OUT" HP_SCRIPT="$script" \
	perl -e 'alarm 60; exec @ARGV' ./switcher > "$OUT/log.txt" 2> "$OUT/err.txt" && status=0 || status=$?
for f in "$OUT"/*.bmp; do [ -f "$f" ] || continue; sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null 2>&1 && rm "$f"; done
echo "exit status $status"; ls "$OUT" | grep png || true
echo "--- recent.txt:"; cat "$CARD/.userdata/shared/.minui/recent.txt"
echo "--- /tmp/switch-to.txt:"; cat /tmp/switch-to.txt 2>/dev/null || echo "(none)"
