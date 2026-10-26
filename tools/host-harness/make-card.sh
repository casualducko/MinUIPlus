#!/bin/sh
# Build a fake SD card with awkward content. usage: make-card.sh CARD_DIR RES_DIR
set -e
CARD=${1:?card dir}; RES=${2:?path to a .system/res folder}
case "$CARD" in */card|*harness*|*/tmp/*) ;; *) echo "make-card.sh: '$CARD' does not look like a test card folder (it is deleted first): use a path ending in /card"; exit 1 ;; esac
rm -rf "$CARD"
mkdir -p "$CARD"/.userdata/macos "$CARD"/.userdata/shared/.minui "$CARD"/.system/res "$CARD"/Roms "$CARD"/Bios "$CARD"/Saves
cp -R "$RES"/. "$CARD/.system/res/"
for e in SFC GB GBC FC; do
	mkdir -p "$CARD/.system/macos/paks/Emus/$e.pak"
	printf '#!/bin/sh\n' > "$CARD/.system/macos/paks/Emus/$e.pak/launch.sh"
done
R="$CARD/Roms"
mkdir -p "$R/Super Nintendo (SFC)" "$R/Super Nintendo (SUPA)" "$R/Game Boy (GB)" \
         "$R/Game Boy Color (GBC)/Sub" "$R/NES (FC)" "$R/Odd (Name" "$R/Mystery (ZZZ)"
touch "$R/Super Nintendo (SFC)/Alpha Quest.sfc" "$R/Super Nintendo (SFC)/Beta Quest.sfc"
touch "$R/Super Nintendo (SUPA)/Hidden Supa Game.sfc"      # no SUPA pak: must not be listed
touch "$R/Game Boy (GB)/Tetris.gb" "$R/Game Boy (GB)/Zelda.gb"
touch "$R/Game Boy (GB)/Wizards & Warriors Chapter X - The Fortress of Fear (USA).gb"
touch "$R/Game Boy (GB)/Wizards & Warriors Chapter X - The Fortress of Fear (Europe).gb"
touch "$R/Game Boy (GB)/Mario Land (USA).gb" "$R/Game Boy (GB)/Mario Land (Europe).gb"
touch "$R/Game Boy Color (GBC)/Pok$(printf '\303\251')mon Gold.gbc" "$R/NES (FC)/QIX.nes"
touch "$R/Odd (Name/odd.gb" "$R/Mystery (ZZZ)/x.bin"
touch "$R/Game Boy (GB)/$(printf 'L%.0s' $(seq 1 200)).gb"   # a 200 character name
