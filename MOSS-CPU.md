# CPU frequency scaling on the RGB20SX (findings, 2026-10-25)

**Status: cause found, fix written, nothing changed on the device.** Testing needs a change to TF1, which waits for an explicit go-ahead.

## What is wrong
- The CPU stays at 1.416 GHz: `/sys/devices/system/cpu/cpu0/cpufreq` does not exist.
- `dmesg`: `fan53555-regulator 0-001c: Failed to get chip ID!` then `cpu cpu0: Failed to get reg` (cpufreq needs the CPU supply regulator, `vdd_cpu`).
- Moss's device tree is the RGB30's. It says the CPU regulator is a **TCS4525 at I2C address 0x1c**.

## What the unit really has
- `i2cdetect -y 0` on this unit: `0x20` (the RK817 PMIC, bound), **`0x40`**, nothing at `0x1c`. Reading `0x1c` fails.
- The chip at `0x40` answers like a Silergy **SYR827** (the fan53555 family): registers 0..6 = `97 97 80 88 01 88 30`.
  - reg 0 and reg 1 (VSEL0/VSEL1) = `0x97`: enabled, selector 0x17 -> 712.5 mV + 23 x 12.5 mV = **1.000 V**, which is what the bootloader left for 1.416 GHz.
  - reg 3 (ID1) = `0x88`: die id 8, the value the kernel's fan53555 driver maps to SYR82x.
- The kernel (4.19.172, built into `/flash/KERNEL`) already has `silergy,syr827` in its fan53555 driver (`CONFIG_REGULATOR_FAN53555=y`, strings checked). No kernel change is needed, only the device tree.
- Other differences from the RGB30 tree seen on the way (not looked into): nothing answers at `0x62` (cw2015 fuel gauge) or `0x10` (es8316 codec) on bus 0.

## The fix
Change the `vdd_cpu` node in the device tree (everything else stays, including the voltage limits and the `vdd_cpu` label that `cpu-supply` points to):

```
-		vdd_cpu: tcs4525@1c {
-			compatible = "tcs,tcs452x";
-			reg = <0x1c>;
+		vdd_cpu: syr827@40 {
+			compatible = "silergy,syr827";
+			reg = <0x40>;
```
(and the `vdd_cpu` alias path to `syr827@40`). Files, outside this repo in `~/powkiddy-rgb20sx/tf1/`:
- `orig-rgb30.dtb` / `orig-rgb30.dts`: copy of `/flash/rk3566-rgb30-linux.dtb` as read from the device (sha256 `0d52e3b1...f778b`).
- `rgb20sx-syr827.dts` / `.dtb`: the same with the two changes above (same size, 117127 bytes; `diff` of the decompiled trees shows only those lines).

## What changes if it works
- cpufreq starts. The tree's OPP table: 408, 600, 816, 1104, 1416, 1608, 1800, 1992 MHz at 0.825 V to 1.15 V. Moss's governor then moves between them: **faster in heavy games (PSX, SNES cores), much lower clocks and power in menus and light games**, but a hotter and thirstier top end (1.992 GHz at 1.15 V) than today's fixed 1.416 GHz at 1.0 V.
- The MinUI CPU setting becomes meaningful. Cap the top speed if it gets too warm.

## Risks
- If the chip at `0x40` is not a SYR827, the regulator driver fails to bind again and nothing is worse than today (the bootloader's setting stays).
- A wrong voltage curve could make a clock step unstable. The tree's table is the RGB30's; the CPU is the same RK3566.
- A dtb that does not boot needs the card in a reader to restore (`/flash` is the vfat partition of TF1).

## Test plan (needs the user's go-ahead for each step)
1. Image TF1 on the Mac (or at least copy `/flash` off) and keep a spare known-good card.
2. Copy `rgb20sx-syr827.dtb` over `/flash/rk3566-rgb30-linux.dtb` (original saved as `orig-rgb30.dtb`; rollback = put it back from the Mac).
3. Reboot. Check: `dmesg | grep fan53555` (should say `FAN53555 Option[8] Rev[...] Detected!`), `ls /sys/devices/system/cpu/cpu0/cpufreq`, `cat .../scaling_available_frequencies`, `cat .../scaling_cur_freq` idle and in a game, `cat /sys/class/regulator/*/name` includes `vdd_cpu` with a changing `microvolts`.
4. Run a heavy game for 10 minutes; watch `thermal_zone0/temp` and the battery; confirm no crashes, audio underruns or sleep/wake problems (the regulator also has a suspend setting).
5. If good: make it permanent in a Moss build (a patch under `moss-src/projects/Rockchip/packages/linux/...`) so it survives image updates.
