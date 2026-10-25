# Moss to-do

Things on the Moss side (the OS on TF1) that MinUIPlus cannot fix from the games card. Rule: **never touch TF1** without an explicit go-ahead; build and test on a spare card first.

Last updated 2026-10-25.

## Facts
- The RGB20SX boots Moss's **RGB30** device tree (`rk3566-rgb30-linux.dtb`). `/proc/device-tree/model` says "Powkiddy RGB30", so the unit cannot name itself (MinUIPlus reads `.userdata/rgb30/model.txt` instead).
- Moss ships as a read-only image: `KERNEL`, `SYSTEM`, `extlinux`, the `.dtb` files, on the first partition (`/flash` on the device).
- The boot splash is Moss's (`/usr/share/plymouth/splash.png`, copy in `/usr/config/splash`). It shows before the games card is read, so no MinUI theme can change it.
- No rumble motor on this unit (tested 2026-10-24).

## 1. CPU stuck at 1.416 GHz (biggest payoff)
- The CPU voltage regulator (TCS4525, I2C address 0x1c) never binds to a driver, so `cpufreq` never starts. MinUI's CPU setting does nothing and the battery pays for it.
- Fix: a device tree change (and maybe a kernel config check) so the regulator binds. Needs a custom image, tested on a spare card.
- **Cause found 2026-10-25:** Moss's tree declares a TCS4525 at I2C 0x1c, but the unit has a SYR827-class regulator at **0x40**. The fix is a two-line device tree change; details and the test plan are in `MOSS-CPU.md`. **Applied to the device 2026-10-25 and tested (boots, scales 600 to 1992 MHz, sleep/wake fine).** Files and the install script are in `moss/`. The Moss kernel patch for it is written and checked against the pinned kernel source (`moss/0004-...`); no image has been built with it yet. Battery comparison skipped (the user can't test it).

## 2. Two untested patches (`moss/0001`, `moss/0002`)
- `0001` restart logind when the sleep config changes (only matters on the first boot of a fresh Moss install; sleep and wake already work on this unit).
- `0002` let ordinary 8 GB cards be games cards (only matters for someone using an 8 GB card).
- Both need a Moss build to test; neither affects the current unit. Low priority.

## 3. Model name, fuel gauge, audio codec (checked 2026-10-25)
- **Fuel gauge and codec: not a problem.** The RGB30 tree lists a cw2015 fuel gauge (0x62) and an es8316 codec (0x10) on I2C bus 0, but both nodes are `disabled`, so nothing answers there. The unit uses the RK817 PMIC for both: sound card `rockchip,rk817-codec`, battery from `rk817-battery` (`/sys/class/power_supply/battery`: capacity 85%, 3.889 V, design 3151 mAh, health Good). The only kernel messages are harmless "property missing" notes from the rk817 drivers.
- **Model name:** ship a device tree that names the unit RGB20SX, if wanted. Until then: `model.txt`. (Needs the same build as item 1.)

## 4. Optional
- **Boot splash:** swap `splash.png` in a custom build.
- **Trimmed Moss:** drop EmulationStation and the extra services; faster boot, less battery, fewer layers in the audio path (IDEAS.md item 21).
- **Build pipeline:** `casualducko/rgb20pro-firmware` (GitHub Actions) and the ccache release in `casualducko/distribution-cache`; the 15-hour builds are the cost of every image.

## Standing safety notes
- Back up TF1 (image of the card) before any change to it.
- Keep a spare card with a known-good Moss for rollback.
- Anything that writes `/flash` needs the user's explicit OK, per action.
