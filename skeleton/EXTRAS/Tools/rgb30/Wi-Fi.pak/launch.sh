#!/bin/sh

cd "$(dirname "$0")"

. /etc/profile # NOTE: this nukes MinUI's PATH modifications
PATH=/storage/roms/.system/rgb30/bin:$PATH

# wifi.elf is the tool (scan, pick a network, type a password, several saved networks).
# launch-old.sh is the earlier one that reads wifi.txt; it runs if wifi.elf isn't here.
if [ ! -x ./wifi.elf ]; then exec ./launch-old.sh; fi
./wifi.elf > ./log.txt 2>&1
