# Moss changes for the RGB20SX

These change the OS on TF1, not the games card. Read `../MOSS.md` and `../MOSS-CPU.md` first. Never write TF1 without a backup of `/flash` and a way to restore it (card reader).

| File | What |
|---|---|
| `0003-...dts.diff` | The CPU regulator fix as a diff of the decompiled device tree: the unit has a SYR827 at I2C 0x40, the RGB30 tree says TCS4525 at 0x1c. With it `cpufreq` works (steps of 408 to 1992 MHz; the launcher menu sits at 600). Tested on the device: boots, scales, sleeps and wakes. |
| `rk3566-rgb30-original.dtb` | Moss's stock `rk3566-rgb30-linux.dtb` (md5 `5afafbc4...b9`), read from the device. |
| `rk3566-rgb30-rgb20sx.dtb` | The same with the 0003 change (md5 `a9fc83cf...54`). |
| `apply-dtb.sh <ip> [revert]` | Installs it (or puts the stock one back) over SSH; checks the current file is one of the two known ones, keeps a copy of the replaced file in `backup-<ip>-<time>/`, verifies the checksum. Reboot afterwards. |
| `0004-kernel-...patch` | The same change as a Moss kernel patch (RK3566 kernel, `rk3568-evb-rgb30.dtsi`, `vdd_cpu` node). Checked: it applies cleanly to the kernel source at the commit Moss pins (`9d98ce7b4`, JustEnoughLinuxOS/rk356x-kernel) and gives the intended file. Also built as a device tree on the Mac (`cpp` + `dtc` on the pinned kernel's `rk3566-rgb30-linux.dts`): the result differs from the unpatched tree by exactly the `vdd_cpu` lines, matching the dtb on the device. **No full Moss image has been built with it.** Also committed in `../moss-src` as `projects/Rockchip/packages/linux/patches/RK3566/0002-...`. It makes the image RGB20SX-specific (a real RGB30 has the TCS4525). |
| `splash-patch.py KERNEL NEW.png OUT` | Puts a new boot splash into a copy of `/flash/KERNEL` (nothing is written to a device). The logo is `/splash/splash.png` in the initramfs inside the kernel file; the new stream is padded to the old length so nothing after it moves. Needs a small PNG (the stream has about 4 KB to spare: a palette PNG of a few KB fits, a full-colour one may not). Checked on the Mac: the patched file is the same length, differs only inside the stream, and unpacks to the same 279 files with only the splash changed. **Not booted on the device.** `/storage/.config/splash/splash.png` is not what the boot draws (tried 2026-10-26: no change). |
| `0001`, `0002` | Untested Moss patches (restart logind on sleep-config change; 8 GB cards as games cards). |
| `0003` (in the Moss fork) | The device tree names the unit "Powkiddy RGB20SX"; the quirks folder is copied under that name (Moss picks its quirks by the model string). Needs a new KERNEL (or device tree) on `/flash`. Kernel build with it ran on the fork 2026-10-28. |
| `repack-system.sh` | Unpacks `/flash/SYSTEM` (squashfs, lzo) in a Linux container, makes the three changes below, packs it again with the same settings. The result is `../tf1/system-repacked/SYSTEM` (+ `SYSTEM.md5`, and `SYSTEM.orig` = the stock file). A listing of both images differs only by those changes. |
| `apply-system.sh <ip> [revert]` | Installs the repacked SYSTEM (and its `.md5`, which Moss checks at boot) over SSH, or puts the stock one back. **Not run yet.** |

What the repacked SYSTEM changes (boot measured at 9.5 s: 3.1 kernel + 6.4 userspace):
1. The hardware database is built into the image (`/usr/lib/udev/hwdb.bin`) and the hwdb unit no longer re-runs when `/etc/udev/hwdb.d` is non-empty (it never is: it holds `99-osmcrf.hwdb`). The stock image rebuilt it on every boot, 2.3 s on the critical path. User-added hwdb files are no longer picked up automatically.
2. `debug-shell.service` is no longer enabled: the stock image runs a root shell with no password on tty9.
3. Nothing else is removed. The idle unit already runs 15 services in about 100 MB; the unused emulators are files only.

For a Moss build the same change belongs in the kernel's RGB30 devicetree (`vdd_cpu`: compatible `silergy,syr827`, `reg = <0x40>`).
