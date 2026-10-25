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
- **Cause found 2026-10-25:** Moss's tree declares a TCS4525 at I2C 0x1c, but the unit has a SYR827-class regulator at **0x40**. The fix is a two-line device tree change; details and the test plan are in `MOSS-CPU.md`. Patched dtb is in `../tf1/rgb20sx-syr827.dtb`. Not applied to the device yet (needs the user's OK to write `/flash`).

## 2. Two untested patches (`../moss-patches/`)
- `0001` restart logind when the sleep config changes (makes the sleep timeout reliable).
- `0002` let ordinary 8 GB cards be games cards.
- Neither has been built into an image or tried on the device.

## 3. Model name
- Ship a device tree that names the unit RGB20SX, if the RGB30's is close enough. Until then: `model.txt`.

## 4. Optional
- **Boot splash:** swap `splash.png` in a custom build.
- **Trimmed Moss:** drop EmulationStation and the extra services; faster boot, less battery, fewer layers in the audio path (IDEAS.md item 21).
- **Build pipeline:** `casualducko/rgb20pro-firmware` (GitHub Actions) and the ccache release in `casualducko/distribution-cache`; the 15-hour builds are the cost of every image.

## Standing safety notes
- Back up TF1 (image of the card) before any change to it.
- Keep a spare card with a known-good Moss for rollback.
- Anything that writes `/flash` needs the user's explicit OK, per action.
