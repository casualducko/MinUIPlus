#!/bin/sh
# Put back what install.sh replaced. Every backup-before-fork-* folder on the card is read, oldest
# first, and each file is restored from the OLDEST backup that has it (the true original, even if
# install.sh was run more than once, or only got part of the way the first time).
#   ./revert.sh /Volumes/RGB20SX
# Files that install.sh only ADDED (new tools, the fallback font) are left in place: delete them by hand.
set -e
card=${1:?usage: revert.sh /path/to/the/card}
backups=$(command ls -dtr "$card"/.system/rgb30/backup-before-fork-* 2>/dev/null || true)
[ -n "$backups" ] || { echo "no backup-before-fork-* folder on $card"; exit 1; }
seen=$(mktemp)
trap 'rm -f "$seen"' EXIT
echo "$backups" | while IFS= read -r backup; do
	echo "reading $backup"
	(cd "$backup" && find . -type f) | while IFS= read -r f; do
		rel=${f#./}
		grep -qxF "$rel" "$seen" && continue # an older backup already had it
		echo "$rel" >> "$seen"
		mkdir -p "$(dirname "$card/$rel")" # (a folder that was moved into the backup, like the old Power tool)
		cp "$backup/$rel" "$card/$rel"
		echo "  restored: $rel"
	done
done
dot_clean -m "$card" 2>/dev/null || true
sync
echo "done. (The backup folders are left in place; delete them when you are happy.)"
