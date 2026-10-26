#!/bin/sh
# Installs the RGB20SX device tree (CPU regulator = SYR827 at I2C 0x40) on a running device over SSH,
# so cpufreq works. Writes /flash on TF1: read MOSS-CPU.md first. Needs Tools > SSH running on the device.
#   moss/apply-dtb.sh <device ip>            install
#   moss/apply-dtb.sh <device ip> revert     put the original RGB30 tree back
# A copy of the file it replaces is kept on this computer in moss/backup-<ip>-<time>/ first.
set -e
here=$(cd "$(dirname "$0")" && pwd)
ip=${1:?device ip}; mode=${2:-apply}
K="-i $HOME/.ssh/rgb20sx_ed25519 -o IdentitiesOnly=yes"
[ -f "$HOME/.ssh/rgb20sx_ed25519" ] || K=""
sum() { md5 -q "$1" 2>/dev/null || md5sum "$1" | cut -d' ' -f1; }
orig=$(sum "$here/rk3566-rgb30-original.dtb"); new=$(sum "$here/rk3566-rgb30-rgb20sx.dtb")
if [ "$mode" = revert ]; then src=$here/rk3566-rgb30-original.dtb; want=$orig; else src=$here/rk3566-rgb30-rgb20sx.dtb; want=$new; fi
now=$(ssh -n $K root@$ip 'md5sum /flash/rk3566-rgb30-linux.dtb' | cut -d' ' -f1)
[ "$now" = "$want" ] && { echo "already in place"; exit 0; }
[ "$now" = "$orig" ] || [ "$now" = "$new" ] || { echo "the device's rk3566-rgb30-linux.dtb ($now) is neither the stock nor the patched one: not touching it"; exit 1; }
b=$here/backup-$ip-$(date +%Y%m%d-%H%M%S); mkdir -p "$b"
scp $K root@$ip:/flash/rk3566-rgb30-linux.dtb "$b/" >/dev/null
scp $K "$src" root@$ip:/tmp/rgb30.dtb.tmp >/dev/null
sent=$(ssh -n $K root@$ip 'md5sum /tmp/rgb30.dtb.tmp' | cut -d' ' -f1)
[ "$sent" = "$want" ] || { echo "the file did not arrive whole ($sent): /flash was not touched"; exit 1; } # never swap in a damaged tree
ssh -n $K root@$ip 'mount -o remount,rw /flash && cp /tmp/rgb30.dtb.tmp /flash/rk3566-rgb30-linux.dtb.new && sync && mv /flash/rk3566-rgb30-linux.dtb.new /flash/rk3566-rgb30-linux.dtb && sync; mount -o remount,ro /flash'
got=$(ssh -n $K root@$ip 'md5sum /flash/rk3566-rgb30-linux.dtb' | cut -d' ' -f1)
if [ "$got" = "$want" ]; then echo "done ($mode); reboot to use it"; exit 0; fi
echo "checksum mismatch after writing ($got): putting back what was there"
scp $K "$b/rk3566-rgb30-linux.dtb" root@$ip:/tmp/rgb30.dtb.back >/dev/null
ssh -n $K root@$ip 'mount -o remount,rw /flash && cp /tmp/rgb30.dtb.back /flash/rk3566-rgb30-linux.dtb.new && sync && mv /flash/rk3566-rgb30-linux.dtb.new /flash/rk3566-rgb30-linux.dtb && sync; mount -o remount,ro /flash'
back=$(ssh -n $K root@$ip 'md5sum /flash/rk3566-rgb30-linux.dtb' | cut -d' ' -f1)
[ "$back" = "$now" ] && echo "restored the file that was there" || echo "RESTORING FAILED ($back): the backup copy is in $b; copy it to /flash/rk3566-rgb30-linux.dtb by hand"
exit 1
