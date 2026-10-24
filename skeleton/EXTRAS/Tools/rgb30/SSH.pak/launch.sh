#!/bin/sh
# Turns Wi-Fi on if it isn't (like Romulus and the Wi-Fi tool), starts Moss's SSH service
# (until the next reboot) and shows the device's address.
# If authorized_key.pub sits next to this file, it is added to /storage/.ssh/authorized_keys.

cd "$(dirname "$0")"
RES_PATH="$(pwd)/res"

. /etc/profile # NOTE: this nukes MinUI's PATH modifications
PATH=/storage/roms/.system/rgb30/bin:$PATH

has_route() { ip route 2>/dev/null | grep -q '^default'; } # by whatever interface: Wi-Fi, a USB tether, Ethernet

{
	date
	if ! has_route; then
		if [ "$(cat /sys/class/net/wlan0/operstate 2>/dev/null)" = "up" ]; then
			echo "wifi: link is up, waiting for an address"   # on already, just no address yet: don't touch it
		else
			echo "wifi: enabling"
			wifictl enable &
		fi
		show.elf "$RES_PATH/enable.png" 30 &
		for i in $(seq 1 30); do
			has_route && break
			sleep 1
		done
		killall -s KILL show.elf 2>/dev/null
		has_route && echo "wifi: connected after ${i}s" || echo "wifi: no connection after 30s"
	fi

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
	say.elf "SSH is on, but Wi-Fi didn't connect.
Check your network (Tools > Wi-Fi), then run this again."
else
	say.elf "SSH is on until you reboot.
Address: $IP"
fi
