#!/bin/sh
# Runs the real Romulus tool on this computer against tools/host-tests/mock_romulus.py,
# with scripted buttons and screenshots (see run.sh for the script syntax; S = START, wait).
#   RES=/path/to/.system/res tools/host-harness/romulus-run.sh "snap:platforms,D,A,wait,wait,snap:games"
# Environment: RES (first time), WORK, OUT, CFG (a config file to use instead of the mock's),
# KEEP=1 (keep the card's Roms and the download list as the last run left them)
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$here/../..
script=${1:?script}
WORK=${WORK:-/tmp/minui-host-harness}
OUT=${OUT:-$WORK/romulus-shots}
CARD=$WORK/card
mkdir -p "$WORK"
if [ ! -d "$CARD" ]; then "$here/make-card.sh" "$CARD" "${RES:?set RES to a .system/res folder}"; fi

rm -rf "$WORK/platform"
cp -R "$repo/workspace/macos/platform" "$WORK/platform"
python3 "$here/platform-patch.py" "$WORK/platform" "$CARD"

# a card with some emulators, ROM folders (GBA is "(MGBA)" like the real card) and no leftovers
for tag in FC MGBA GB GBC; do
	mkdir -p "$CARD/.system/macos/paks/Emus/$tag.pak"
	echo '#!/bin/sh' > "$CARD/.system/macos/paks/Emus/$tag.pak/launch.sh"
done
if [ -z "$KEEP" ]; then rm -rf "$CARD/Roms" "$CARD/.userdata/macos/romulus"* ; fi
mkdir -p "$CARD/Roms/NES (FC)" "$CARD/Roms/GBA (MGBA)" "$CARD/.userdata/macos"

tok="harness-token-$$"
python3 "$repo/tools/host-tests/mock_romulus.py" "$tok" > "$WORK/mock.log" 2>&1 &
mock=$!
trap 'kill $mock 2>/dev/null' EXIT
port=
for i in $(seq 1 20); do port=$(sed -n 's/^PORT //p' "$WORK/mock.log" | head -1); [ -n "$port" ] && break; sleep 0.25; done
if [ -n "$CFG" ]; then cp "$CFG" "$CARD/.userdata/macos/romulus.cfg"
else printf 'url=http://127.0.0.1:%s\ntoken=%s\ninsecure_http=1\n' "$port" "$tok" > "$CARD/.userdata/macos/romulus.cfg"; fi

cd "$repo/workspace/all/romulus"
clang romulus.c json.c zipx.c rapi.c ../titleart/titleart_core.c ../titleart/titleart_gen.c -I../titleart ../common/scaler.c ../common/utils.c ../common/api.c "$WORK/platform/platform.c" \
	-o "$WORK/romulus" -I. -I../common -I"$WORK/platform" -I/opt/homebrew/include -L/opt/homebrew/lib \
	-DPLATFORM=\"macos\" -DUSE_SDL2 -Wno-builtin-macro-redefined -U__ARM_ARCH -D__ARM_ARCH=4 \
	-O1 -g -std=gnu99 -fno-common -Wall -Wno-unused-function -Wno-tautological-constant-out-of-range-compare \
	-Wno-asm-operand-widths -Wno-deprecated-declarations \
	-lSDL2 -lSDL2_image -lSDL2_ttf -lpthread -lm -lz

rm -rf "$OUT"; mkdir -p "$OUT"
cd "$WORK"
if [ -n "$LEAKS_AFTER" ]; then
	# LEAKS_AFTER=N: let the script run (end it with plenty of waits), then ask macOS `leaks` after N seconds
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HP_OUT="$OUT" HP_SCRIPT="$script" ./romulus > "$OUT/log.txt" 2> "$OUT/err.txt" &
	rpid=$!
	sleep "$LEAKS_AFTER"
	if kill -0 $rpid 2>/dev/null; then leaks $rpid > "$OUT/leaks.txt" 2>&1; kill $rpid; else echo "romulus had already exited"; fi
	wait $rpid 2>/dev/null || true
	grep -E "leaks for|ROOT LEAK" "$OUT/leaks.txt" 2>/dev/null | head -20
	status=leaks
else
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HP_OUT="$OUT" HP_SCRIPT="$script" \
	perl -e 'alarm 120; exec @ARGV' ./romulus > "$OUT/log.txt" 2> "$OUT/err.txt" && status=0 || status=$?
fi
for f in "$OUT"/*.bmp; do
	[ -f "$f" ] || continue
	sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null 2>&1 && rm "$f"
done
echo "exit status $status"; ls "$OUT" | grep png || true
echo "--- card Roms:"; find "$CARD/Roms" -type f | sed "s|$CARD/||"
