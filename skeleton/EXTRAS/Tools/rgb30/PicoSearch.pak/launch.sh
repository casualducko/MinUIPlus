#!/bin/sh

cd "$(dirname "$0")"

. /etc/profile # NOTE: this nukes MinUI's PATH modifications
PATH=/storage/roms/.system/rgb30/bin:$PATH

# pico.elf switches Wi-Fi on (wifictl) if it has to, then searches the Lexaloffle BBS
./pico.elf > ./log.txt 2>&1

# PLAY on the download confirmation: the tool leaves the command in /tmp/pico-play. It can't go
# through /tmp/next (MinUI.pak deletes that once a tool returns), so run the emulator from here.
PLAY=/tmp/pico-play
if [ -f "$PLAY" ]; then
	CMD=$(cat "$PLAY")
	rm -f "$PLAY"
	PATH=/storage/roms/.system/rgb30/bin:$PATH
	eval "$CMD"
fi
