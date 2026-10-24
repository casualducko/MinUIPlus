#!/bin/sh
# Starts Moss's SSH service (until the next reboot) and shows the device's address.
# If authorized_key.pub sits next to this file, it is added to /storage/.ssh/authorized_keys.

cd "$(dirname "$0")"

. /etc/profile # NOTE: this nukes MinUI's PATH modifications
PATH=/storage/roms/.system/rgb30/bin:$PATH

{
	date
	if [ -f ./authorized_key.pub ]; then
		mkdir -p /storage/.ssh
		chmod 700 /storage/.ssh
		touch /storage/.ssh/authorized_keys
		KEY=$(cat ./authorized_key.pub)
		grep -qxF "$KEY" /storage/.ssh/authorized_keys || echo "$KEY" >> /storage/.ssh/authorized_keys
		chmod 600 /storage/.ssh/authorized_keys
	fi
	systemctl start sshd
	sleep 1
	STATE=$(systemctl is-active sshd)
	IP=$(ip -4 -o addr show wlan0 | awk '{print $4}' | cut -d/ -f1)
	echo "sshd: $STATE ip: $IP"
} > ./log.txt 2>&1

if [ "$STATE" != "active" ]; then
	say.elf "Couldn't start SSH.
See SSH.pak/log.txt on the card."
elif [ -z "$IP" ]; then
	say.elf "SSH is on, but Wi-Fi isn't.
Turn Wi-Fi on (Tools > Wi-Fi), then run this again."
else
	say.elf "SSH is on until you reboot.
Address: $IP"
fi
