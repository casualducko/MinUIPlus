#!/bin/sh
# Installs the repacked Moss root filesystem (SYSTEM) on a running device over SSH, or puts the stock one back.
# It writes /flash on TF1: read moss/README.md first, take a copy of the card, test on a spare card first.
#   moss/apply-system.sh <device ip>            install ../tf1/system-repacked/SYSTEM
#   moss/apply-system.sh <device ip> revert     put back the original (SYSTEM.orig next to it)
# Moss checks SYSTEM against SYSTEM.md5 when it boots, so both files are replaced; the new file is written
# beside the old one and checked before anything is swapped.
set -e
here=$(cd "$(dirname "$0")" && pwd)
dir=$here/../../tf1/system-repacked
ip=${1:?device ip}; mode=${2:-apply}
K="-i $HOME/.ssh/rgb20sx_ed25519 -o IdentitiesOnly=yes"
sum() { md5 -q "$1" 2>/dev/null || md5sum "$1" | cut -d' ' -f1; }
if [ "$mode" = revert ]; then src=$dir/SYSTEM.orig; else src=$dir/SYSTEM; fi
want=$(sum "$src")
now=$(ssh -n $K root@$ip 'md5sum /flash/SYSTEM' | cut -d' ' -f1)
[ "$now" = "$want" ] && { echo "already in place"; exit 0; }
[ "$now" = "$(sum "$dir/SYSTEM")" ] || [ "$now" = "$(sum "$dir/SYSTEM.orig")" ] || { echo "the device's SYSTEM ($now) is neither the stock nor the repacked one: not touching it"; exit 1; }
free=$(ssh -n $K root@$ip "df -k /flash | tail -1" | awk '{print $4}')
[ "$free" -gt 400000 ] || { echo "not enough room on /flash ($free KB free)"; exit 1; }
ssh -n $K root@$ip 'mount -o remount,rw /flash'
trap 'ssh -n $K root@$ip "rm -f /flash/SYSTEM.new /flash/SYSTEM.md5.new; mount -o remount,ro /flash" >/dev/null 2>&1' EXIT
cat "$src" | ssh $K root@$ip 'cat > /flash/SYSTEM.new && sync'
sent=$(ssh -n $K root@$ip 'md5sum /flash/SYSTEM.new' | cut -d' ' -f1)
[ "$sent" = "$want" ] || { echo "the file did not arrive whole ($sent): SYSTEM was not touched"; exit 1; }
printf '%s  target/SYSTEM\n' "$want" | ssh $K root@$ip 'cat > /flash/SYSTEM.md5.new && sync'
ssh -n $K root@$ip 'mv /flash/SYSTEM.new /flash/SYSTEM && mv /flash/SYSTEM.md5.new /flash/SYSTEM.md5 && sync'
got=$(ssh -n $K root@$ip 'md5sum /flash/SYSTEM' | cut -d' ' -f1)
[ "$got" = "$want" ] && echo "done ($mode); reboot to use it" || { echo "MISMATCH after writing ($got): do not reboot; run revert"; exit 1; }
