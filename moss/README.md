# Moss changes for the RGB20SX

These change the OS on TF1, not the games card. Read `../MOSS.md` and `../MOSS-CPU.md` first. Never write TF1 without a backup of `/flash` and a way to restore it (card reader).

| File | What |
|---|---|
| `0003-...dts.diff` | The CPU regulator fix as a diff of the decompiled device tree: the unit has a SYR827 at I2C 0x40, the RGB30 tree says TCS4525 at 0x1c. With it `cpufreq` works (408 to 1992 MHz). Tested on the device: boots, scales, sleeps and wakes. |
| `rk3566-rgb30-original.dtb` | Moss's stock `rk3566-rgb30-linux.dtb` (md5 `5afafbc4...b9`), read from the device. |
| `rk3566-rgb30-rgb20sx.dtb` | The same with the 0003 change (md5 `a9fc83cf...54`). |
| `apply-dtb.sh <ip> [revert]` | Installs it (or puts the stock one back) over SSH; checks the current file is one of the two known ones, keeps a copy of the replaced file in `backup-<ip>-<time>/`, verifies the checksum. Reboot afterwards. |
| `0001`, `0002` | Untested Moss patches (restart logind on sleep-config change; 8 GB cards as games cards). |

For a Moss build the same change belongs in the kernel's RGB30 devicetree (`vdd_cpu`: compatible `silergy,syr827`, `reg = <0x40>`); the kernel source is not in this repo, so that patch is not written yet.
