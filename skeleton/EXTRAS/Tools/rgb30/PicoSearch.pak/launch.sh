#!/bin/sh

cd "$(dirname "$0")"

. /etc/profile # NOTE: this nukes MinUI's PATH modifications
PATH=/storage/roms/.system/rgb30/bin:$PATH

# Wi-Fi off with two or more saved networks: ask which one to use (the Wi-Fi tool's list). B cancels.
if ! ip route 2>/dev/null | grep -q '^default' && [ -x ../Wi-Fi.pak/wifi.elf ]; then
	( . /etc/profile; PATH=/storage/roms/.system/rgb30/bin:$PATH; cd ../Wi-Fi.pak && ./wifi.elf --pick >/dev/null 2>&1 ) || exit 0
fi

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
