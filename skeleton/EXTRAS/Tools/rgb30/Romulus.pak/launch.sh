#!/bin/sh

cd "$(dirname "$0")"

. /etc/profile # NOTE: this nukes MinUI's PATH modifications
PATH=/storage/roms/.system/rgb30/bin:$PATH

# the network: switch Wi-Fi on if it's off (the Wi-Fi tool is what sets the network up)
if [ "$(cat /sys/class/net/wlan0/operstate 2>/dev/null)" = "down" ]; then
	wifictl enable &
fi

./romulus.elf > ./log.txt 2>&1
