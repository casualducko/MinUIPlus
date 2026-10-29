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

# Files this package ships are replaced when the card already has them; the new tools and a few other files
# (the case list below) are added even when it does not. A copy that fails (card full, read-only) is reported
# and stops the install before the version note is written.
cd "$here"
failed=$(mktemp)
trap 'rm -f "$failed"' EXIT
copy() { # file from the package, then in place in one step (a running program can't be overwritten, but it can be replaced)
	if cp "$1" "$card/$1.new" && mv "$card/$1.new" "$card/$1"; then
		case "$1" in *.elf|*.sh) chmod +x "$card/$1" 2>/dev/null || true ;; esac
		return 0
	fi
	rm -f "$card/$1.new"
	echo "  FAILED: $1"
	echo 1 > "$failed"
	return 1
}
find .system Tools Emus Roms Saves Bios -type f 2>/dev/null | while IFS= read -r f; do
	case "$f" in
		Emus/rgb30/*|Roms/*/.keep|Saves/*/.keep|Bios/*/.keep|Tools/rgb30/Romulus.pak/*|Tools/rgb30/Switcher.pak/*|Tools/rgb30/PicoSearch.pak/*|.system/rgb30/bin/wifiup|.system/res/fallback.ttf|.system/res/fallback-OFL.txt|Tools/rgb30/Stats.pak/*|Tools/rgb30/Controls.pak/*|Tools/rgb30/Themes.pak/*|Tools/rgb30/ASCIIArt.pak/*|Tools/rgb30/Info.pak/*|Tools/rgb30/Update.pak/*|Tools/rgb30/Screenshots.pak/*|Tools/rgb30/Settings.pak/*|Tools/rgb30/SSH.pak/*|Tools/rgb30/Wi-Fi.pak/wifi.elf|Tools/rgb30/Wi-Fi.pak/launch-old.sh) # a new tool: added (and replaced next time), nothing to back up the first time
			mkdir -p "$card/$(dirname "$f")"
			if [ -f "$card/$f" ]; then mkdir -p "$backup/$(dirname "$f")"; cp -p "$card/$f" "$backup/$f"; fi
			copy "$f" && echo "  added/replaced: $f"
			continue ;;
	esac
	[ -f "$card/$f" ] || { echo "  skip (not on the card): $f"; continue; }
	mkdir -p "$backup/$(dirname "$f")"
	cp -p "$card/$f" "$backup/$f"
	copy "$f" && echo "  replaced: $f"
done
[ -s "$failed" ] && { echo "some files could not be copied: the install is incomplete (nothing was marked as installed). $backup has what it replaced."; exit 1; }
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
