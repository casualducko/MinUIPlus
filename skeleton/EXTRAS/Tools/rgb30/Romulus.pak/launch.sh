#!/bin/sh

cd "$(dirname "$0")"

. /etc/profile # NOTE: this nukes MinUI's PATH modifications
PATH=/storage/roms/.system/rgb30/bin:$PATH

# romulus.elf checks the network itself and switches Wi-Fi on (wifictl) if it has to
./romulus.elf > ./log.txt 2>&1
