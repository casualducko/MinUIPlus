#!/bin/sh
# Runs the real Title Art tool on this computer against a fake card with the sample games, with
# scripted buttons and screenshots (script syntax as in run.sh). The tiles land in the card's Roms/*/.res.
#   RES=/path/to/.system/res tools/host-harness/titleart-run.sh "snap:home,A,sleep:4000,snap:done"
# Environment: RES (first time), WORK, OUT, THEME_FILE (printf %b text for theme.cfg, optional),
# TITLEART_ARGS (eg. --auto: no screen), KEEP=1 (keep the card as the last run left it)
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$here/../..
script=${1:?script}
WORK=${WORK:-/tmp/minui-host-harness}
OUT=${OUT:-$WORK/titleart-shots}
CARD=$WORK/card
mkdir -p "$WORK"
if [ ! -d "$CARD" ]; then "$here/make-card.sh" "$CARD" "${RES:?set RES to a .system/res folder}"; fi
rm -rf "$WORK/platform"
cp -R "$repo/workspace/macos/platform" "$WORK/platform"
python3 "$here/platform-patch.py" "$WORK/platform" "$CARD"
if [ -z "$KEEP" ]; then
rm -rf "$CARD/Roms" "$CARD/.userdata/macos/titleart-made.txt" "$CARD/.userdata/macos/theme.cfg"
mkdir -p "$CARD/.userdata/macos"
[ -n "$THEME_FILE" ] && printf '%b' "$THEME_FILE" > "$CARD/.userdata/macos/theme.cfg"
python3 - "$CARD" <<'PY'
import os, sys
card = sys.argv[1]
games = {
 "GBA (MGBA)": ["Apotris.gba", "Pokemon ROWE (v2.02).gba", "Pokemon Heart and Soul (v1.2.1).gba", "ポケットモンスター エメラルド.gba", "ゼルダの伝説 神々のトライフォース.gba", "ロックマンX - ドクターワイリーの罠.gba", "星のカービィ 夢の泉の物語.gba", "スーパーマリオランド２ - 6つの金貨 (Japan).gba", "ドラゴンクエストモンスターズ キャラバンハート 特別版 ダブルパック.gba"],
 "Genesis (MD)": ["Yazzie.bin", "Rocket Knight Adventures.md", "NHL 95 All-Time (Widescreen).bin"],
 "SNES (SFC)": ["Mega Man X.sfc", "Legend of Zelda - 18 Hours Past (v1.12).sfc", "Illusion of Gaia Retranslated (v1.1).sfc", "Legend of Zelda, The (USA).sfc"],
 "GBC (GBC)": ["Dragon's Lair.gbc", "Super Mario Land 2 DX - 6 Golden Coins.gb", "Pocket Bomberman.gbc", "Donkey Kong.gb"],
 "Playstation (PS)": ["Castlevania - Symphony of the Night.chd"],
 "Nintendo Entertainment System (FC)": ["Mega Man (USA).nes"],
}
for folder, files in games.items():
    d = os.path.join(card, "Roms", folder)
    os.makedirs(d, exist_ok=True)
    for f in files:
        open(os.path.join(d, f), "w").write("x")
    open(os.path.join(d, "Mega Man.sav" if "FC" in folder else "x.sav"), "w").write("x") # a save file: not a game
os.makedirs(os.path.join(card, "Roms", "GBC (GBC)", ".res"), exist_ok=True)
open(os.path.join(card, "Roms", "GBC (GBC)", ".res", "Donkey Kong.gb.png"), "wb").write(b"user's own art")
PY
fi
cd "$repo/workspace/all/titleart"
clang titleart.c titleart_core.c titleart_gen.c ../common/scaler.c ../common/utils.c ../common/api.c "$WORK/platform/platform.c" \
	-o "$WORK/titleart" -I. -I../common -I"$WORK/platform" -I/opt/homebrew/include -L/opt/homebrew/lib \
	-DPLATFORM=\"macos\" -DUSE_SDL2 -Wno-builtin-macro-redefined -U__ARM_ARCH -D__ARM_ARCH=4 \
	-O1 -g -std=gnu99 -fno-common -Wall -Wno-unused-function -Wno-tautological-constant-out-of-range-compare \
	-Wno-asm-operand-widths -Wno-deprecated-declarations \
	-lSDL2 -lSDL2_image -lSDL2_ttf -lpthread -lm -lz 2>&1 | grep -E "error|titleart.*warning" || true
rm -rf "$OUT"; mkdir -p "$OUT"
cd "$WORK"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HP_OUT="$OUT" HP_SCRIPT="$script" \
	perl -e 'alarm 90; exec @ARGV' ./titleart $TITLEART_ARGS > "$OUT/log.txt" 2> "$OUT/err.txt" && status=0 || status=$?
for f in "$OUT"/*.bmp; do [ -f "$f" ] || continue; sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null 2>&1 && rm "$f"; done
echo "exit status $status"; ls "$OUT" | grep png || true
find "$CARD/Roms" -path "*/.res/*" | sort
