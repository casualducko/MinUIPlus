#!/bin/sh
# Adds MinUI's "extras" (the second zip of a stock release) to a package folder, so one package carries everything:
# the extra systems' emulator paks with their cores (Game Gear, Master System, PC Engine, Super Game Boy, Virtual Boy,
# Pokemon mini, Neo Geo Pocket, SNES (Supafaust and snes9x), PICO-8) and the empty Roms / Saves / Bios folders for them.
#   ./stage-extras.sh <package folder>
# Cores come from workspace/rgb30/cores/output, except the mGBA core, which is the pinned one already in the package
# (MGBA.pak): newer mGBA builds crash GBA games on this unit, and Super Game Boy uses mGBA too.
set -e
src=${1:?usage: stage-extras.sh <package folder>}
here=$(cd "$(dirname "$0")/../.." && pwd)
extras=$here/skeleton/EXTRAS
cores=$here/workspace/rgb30/cores/output
mgba=$src/Emus/rgb30/MGBA.pak/mgba_libretro.so
[ -f "$mgba" ] || { echo "no pinned mGBA core at $mgba"; exit 1; }
for pak in "$extras"/Emus/rgb30/*.pak; do
	name=$(basename "$pak")
	dest=$src/Emus/rgb30/$name
	mkdir -p "$dest"
	for f in "$pak"/*; do cp "$f" "$dest/"; done
	# a pak whose launch.sh looks in its own folder needs the core there (the others use the stock cores of the card)
	if grep -q 'CORES_PATH=$(dirname "$0")' "$pak/launch.sh"; then
		exe=$(sed -n 's/^EMU_EXE=//p' "$pak/launch.sh")
		if [ "$exe" = mgba ]; then core=$mgba; else core=$cores/${exe}_libretro.so; fi
		[ -f "$core" ] || { echo "no core for $name: $core"; exit 1; }
		[ "$core" -ef "$dest/${exe}_libretro.so" ] || cp "$core" "$dest/${exe}_libretro.so" # (MGBA.pak already holds the pinned one)
	fi
done
for d in Roms Saves Bios; do # (empty folders with a .keep file: a first install gets the folders, a card that has them is left alone)
	(cd "$extras" && find $d -type f -name .keep) | while IFS= read -r f; do
		mkdir -p "$src/$(dirname "$f")"; cp "$extras/$f" "$src/$f"
	done
done
echo "extras staged in $src: $(ls "$src/Emus/rgb30" | wc -l | tr -d ' ') emulator paks"
