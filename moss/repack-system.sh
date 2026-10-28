set -e
apt-get update -qq >/dev/null && apt-get install -y -qq squashfs-tools >/dev/null 2>&1
R=/work/root
cat $R/usr/lib/systemd/system/debugconfig.service | grep -v '^#' | grep -v '^$'
# 1. hardware database built once, here
cp $R/usr/config/hwdb.d/99-osmcrf.hwdb $R/usr/lib/udev/hwdb.d/
chroot $R /usr/bin/systemd-hwdb --usr update
ls -la $R/usr/lib/udev/hwdb.bin
# the unit then only runs when the database is missing (it used to run on every boot: the user hwdb folder is never empty)
sed -i '/^ConditionDirectoryNotEmpty=|\/etc\/udev\/hwdb.d\/$/d' $R/usr/lib/systemd/system/systemd-hwdb-update.service
grep Condition $R/usr/lib/systemd/system/systemd-hwdb-update.service
# 2. no open root shell on tty9
rm -f $R/usr/lib/systemd/system/sysinit.target.wants/debug-shell.service
# 3. the root password is made once (first boot), not rotated at every boot: the rotation cost 0.4 s (openssl, cryptpw, smbpasswd, syncthing) and SSH here is keys-only
F=$R/usr/lib/autostart/common/007-rootpw
grep -q 'RGB20SX' $F || sed -i '/^tocon "Setting root password..."/i # RGB20SX: set once, not at every boot (0.4 s)\n[ -n "$(get_setting root.password)" ] \&\& exit 0\n' $F
sed -n 1,16p $F
# 4. Moss picks its device quirks by the device tree's model string. The unit's tree will say "Powkiddy RGB20SX" (moss-src patch 0003,
# rk3566-rgb30-rgb20sx-named.dtb): the RGB30 quirks (governors, gpios, suspend, display, audio path) are there under that name too, so the
# image works with either tree. Without this folder the renamed tree would boot with no quirks applied.
Q=$R/usr/lib/autostart/quirks/devices
[ -d "$Q/Powkiddy RGB20SX" ] || cp -a "$Q/Powkiddy RGB30" "$Q/Powkiddy RGB20SX"
ls "$Q/Powkiddy RGB20SX"
rm -f /sq/SYSTEM.new
mksquashfs $R /sq/SYSTEM.new -comp lzo -Xcompression-level 9 -b 524288 -no-xattrs -noappend -quiet
unsquashfs -s /sq/SYSTEM.new | sed -n 1,12p
