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
			echo "wifi: enabling (the set-up network, then the other saved ones)"
			wifiup &
		fi
		show.elf "$RES_PATH/enable.png" 45 &
		for i in $(seq 1 45); do
			has_route && break
			sleep 1
		done
		killall -s KILL show.elf 2>/dev/null
		has_route && echo "wifi: connected after ${i}s" || echo "wifi: no connection after 45s"
	fi

	if [ -f ./authorized_key.pub ]; then
		mkdir -p /storage/.ssh
		chmod 700 /storage/.ssh
		touch /storage/.ssh/authorized_keys
		# a last line with no newline would have the next key glued to it
		if [ -s /storage/.ssh/authorized_keys ] && [ "$(tail -c1 /storage/.ssh/authorized_keys | wc -l)" -eq 0 ]; then
			echo >> /storage/.ssh/authorized_keys
		fi
		# every key in the file (one per line), each added once
		while IFS= read -r KEY; do
			[ -n "$KEY" ] || continue
			# a line that is not a public key (a private key pasted by mistake, a comment) is not installed: passwords are switched off below
			if command -v ssh-keygen >/dev/null 2>&1 && ! printf '%s\n' "$KEY" | ssh-keygen -l -f /dev/stdin >/dev/null 2>&1; then
				echo "skipped a line that is not a valid public key"
				continue
			fi
			grep -qxF "$KEY" /storage/.ssh/authorized_keys || echo "$KEY" >> /storage/.ssh/authorized_keys
		done < ./authorized_key.pub
		chmod 600 /storage/.ssh/authorized_keys
	fi
	# Moss lets root log in with a password, and its default is public: with a key in place, switch
	# password logins off, so nobody else on this Wi-Fi gets in. (With no key at all it is left as the
	# system has it: turning passwords off would lock everyone out.)
	RESTART=0
	if [ -s /storage/.ssh/authorized_keys ]; then
		CONF=/storage/.cache/services/sshd.conf
		mkdir -p /storage/.cache/services
		touch "$CONF"
		if ! grep -q 'PasswordAuthentication' "$CONF"; then
			echo 'SSH_ARGS="-o PasswordAuthentication=no -o KbdInteractiveAuthentication=no"' >> "$CONF"
			RESTART=1
		fi
		echo "sshd: passwords off, keys only"
	else
		echo "sshd: no key installed, the system's own login settings apply"
	fi
	if [ "$RESTART" = 1 ]; then systemctl restart sshd; else systemctl start sshd; fi
	sleep 1
	STATE=$(systemctl is-active sshd)
	IP=$(ip -4 -o addr show wlan0 | awk '{print $4}' | cut -d/ -f1)
	echo "sshd: $STATE ip: $IP"
} > ./log.txt 2>&1

if [ "$STATE" != "active" ]; then
	say.elf "Couldn't start SSH.

See SSH.pak/log.txt
on the card."
elif [ -z "$IP" ]; then
	say.elf "SSH is on, but Wi-Fi
didn't connect.

Check your network
(Tools > Wi-Fi),

then run this again."
else
	say.elf "SSH is on until you reboot.
Address: $IP"
fi
