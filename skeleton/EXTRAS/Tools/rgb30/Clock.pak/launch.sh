#!/bin/sh

cd "$(dirname "$0")"
# Wi-Fi off with two or more saved networks: ask which one to use (the Wi-Fi tool's list). B cancels.
if ! ip route 2>/dev/null | grep -q '^default' && [ -x ../Wi-Fi.pak/wifi.elf ]; then
	( . /etc/profile; PATH=/storage/roms/.system/rgb30/bin:$PATH; cd ../Wi-Fi.pak && ./wifi.elf --pick >/dev/null 2>&1 ) || exit 0
fi

./clock.elf
