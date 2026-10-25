#!/bin/sh
# Copy this build onto a MinUI card, keeping a backup of everything it replaces.
#   ./install.sh /Volumes/RGB20SX          (the TF2 / games card)
# To undo, run ./revert.sh with the same card.
set -e
here=$(cd "$(dirname "$0")" && pwd)
card=${1:?usage: install.sh /path/to/the/card (the one with a .system folder)}
[ -d "$card/.system/rgb30/bin" ] || { echo "no .system/rgb30/bin on $card, is that the right card?"; exit 1; }

stamp=$(date +%Y%m%d-%H%M%S)
backup="$card/.system/rgb30/backup-before-fork-$stamp"
mkdir -p "$backup"
echo "backing up to $backup"

# only files this package ships AND the card already has are replaced
cd "$here"
find .system Tools Emus -type f | while read f; do
	case "$f" in
		Tools/rgb30/Romulus.pak/*|.system/res/fallback.ttf|.system/res/fallback-OFL.txt|Tools/rgb30/Stats.pak/*|Tools/rgb30/Themes.pak/*|Tools/rgb30/ASCIIArt.pak/*|Tools/rgb30/Info.pak/*|Tools/rgb30/Update.pak/*|Tools/rgb30/Screenshots.pak/*|Tools/rgb30/Settings.pak/*|Tools/rgb30/SSH.pak/*|Tools/rgb30/Wi-Fi.pak/wifi.elf|Tools/rgb30/Wi-Fi.pak/launch-old.sh) # a new tool: added (and replaced next time), nothing to back up the first time
			mkdir -p "$card/$(dirname "$f")"
			if [ -f "$card/$f" ]; then mkdir -p "$backup/$(dirname "$f")"; cp -p "$card/$f" "$backup/$f"; fi
			cp "$f" "$card/$f.new" && mv "$card/$f.new" "$card/$f"; chmod +x "$card/$f" 2>/dev/null || true
			echo "  added/replaced: $f"
			continue ;;
	esac
	[ -f "$card/$f" ] || { echo "  skip (not on the card): $f"; continue; }
	mkdir -p "$backup/$(dirname "$f")"
	cp -p "$card/$f" "$backup/$f"
	cp "$f" "$card/$f.new" && mv "$card/$f.new" "$card/$f" # a running program can't be overwritten in place, but it can be replaced
	echo "  replaced: $f"
done
# tools that were renamed: the old folder goes into the backup, so the Tools list shows one
for old in Power:Settings Theme:Themes TitleArt:ASCIIArt; do
	name=${old%%:*}; now=${old##*:}
	if [ -d "$card/Tools/rgb30/$name.pak" ]; then
		mkdir -p "$backup/Tools/rgb30"
		mv "$card/Tools/rgb30/$name.pak" "$backup/Tools/rgb30/$name.pak"
		echo "  retired the old $name tool (it is $now now)"
	fi
done
# the Romulus tool reads its server from here; an empty one to fill in (never overwritten)
cfg="$card/.userdata/rgb30/romulus.cfg"
if [ -d "$card/.userdata/rgb30" ] && [ ! -f "$cfg" ]; then
	cat > "$cfg" <<'CFG'
# Romulus server for the Romulus tool. Edit this file on a computer.
# The url must be https:// (the token is sent with every request).
url=
token=

# Optional: file a platform from the server in a particular ROM folder,
# one line per platform, eg.
# map=gba:Game Boy Advance (MGBA)
CFG
	echo "  created an empty .userdata/rgb30/romulus.cfg: fill in url and token"
fi
# the Update tool reads its source from here; never overwritten
ucfg="$card/.userdata/rgb30/update.cfg"
if [ -d "$card/.userdata/rgb30" ] && [ ! -f "$ucfg" ]; then
	cat > "$ucfg" <<'CFG'
# Where Tools > Update looks for new builds.
repo=casualducko/MinUIPlus
# For a private repo: a GitHub token that can read it (fine-grained, Contents: read). Keep this file private.
token=
CFG
	echo "  created .userdata/rgb30/update.cfg"
fi
# which build this is, for Tools > Update
if [ -f "$here/VERSION" ] && [ -d "$card/.system/rgb30" ]; then cp "$here/VERSION" "$card/.system/rgb30/fork-version.txt"; fi
dot_clean -m "$card" 2>/dev/null || true
sync
echo "done. $backup holds the files it replaced."
