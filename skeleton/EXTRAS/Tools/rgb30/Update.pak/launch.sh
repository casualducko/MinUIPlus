#!/bin/sh

cd "$(dirname "$0")"

. /etc/profile # NOTE: this nukes MinUI's PATH modifications
PATH=/storage/roms/.system/rgb30/bin:$PATH

U=/storage/roms/.userdata/rgb30/update
rm -f /tmp/update-ready

# update.elf checks the network itself, downloads and verifies the package, unpacks it to $U/stage and
# leaves a marker. The install runs from here because it replaces update.elf (and this script) on the card.
./update.elf > ./log.txt 2>&1

if [ -f /tmp/update-ready ]; then
	rm -f /tmp/update-ready
	mkdir -p "$U"
	if [ -f "$U/stage/install.sh" ] && sh "$U/stage/install.sh" /storage/roms > "$U/install-log.txt" 2>&1; then
		echo ok > "$U/result.txt"
	else
		echo failed > "$U/result.txt"
	fi
	rm -rf /storage/roms/.userdata/rgb30/update/stage
	rm -f "$U/package.tar.gz" "$U/package.sha256"
	./update.elf --done > ./log.txt 2>&1
fi
