# Ideas for the RGB20SX fork

Last refreshed 2026-10-24 (evening). `FORK.md` lists what has been done; this is what is open and what could come next. Sizes are rough: **S** is an evening, **M** a few evenings, **L** a project. "Device" means it needs the unit in hand to finish.

## Where things stand

**Working and confirmed on the device:** volume direction, NES audio (rate control), no pop at game start, volume/brightness pill in games, Fn as MENU, the Sleep & Power settings, screenshots (MENU+SELECT), the left stick (it was already a D-pad in "Standard" mode; the deadzone is now a setting), the timezone fix (the clock showed UTC because Moss's boot step that applies the zone is skipped), the numbered slot picker, the smaller margins (`PADDING` 20), the big-row Options pages, remote installs over SSH (Tools > SSH).

**Built and tested only on the Mac or by reading logs, not yet confirmed by you on the device:** Restore Save (needs a save that has changed a few times), low-battery shutdown (fires at 5%; the code is running, per the logs), the Settings tool's Rumble and Audio buffer rows, **Tools > Wi-Fi v2**, **rumble itself**, Romulus end to end (never reported).

### What we learned that is not in the code
- **The CPU is stuck at 1.416 GHz.** The CPU's voltage regulator (a TCS4525 on I2C bus 0, address 0x1c) never binds to a driver ("cpu0 policy NULL"), so `cpufreq` never starts and MinUI's CPU speed writes do nothing. Fixing it means changing Moss's device tree on TF1; not attempted. Gameplay is nowhere near CPU-bound anyway (the SNES emulator thread used about 38% of one core).
- **The Zelda dropouts we chased were menu time and game start**, not slowness. Frame timing in gameplay was a steady 60 fps; the 80 ms audio buffer was not needed (Default again).
- **The Zelda pot-break sound** is in all three SNES cores' output at about the music's level (the real console's recording shows a louder crash). Cause unknown, not the device (PipeWire passes it intact; the speaker plays 0.5 to 15 kHz equally).
- **mGBA updates:** `517e518` and `925f0f0` both segfaulted when loading a GBA game on the device; the old core is back. Upstream moved libretro to CMake right after `517e518`.
- Sysfs files `stat` as regular files; writing them needs a plain write (a temp-file-then-rename breaks them).

### Still open
| Item | Notes |
|---|---|
| Silent audio now and then | Cause unknown, not seen again since the diagnostics went in. If it recurs: power off, take the card, read `.userdata/rgb30/logs/<system>.txt`. |
| mGBA / other core updates | Cores are pinned to Nov 2024 commits (mgba 823 behind, fceumm 390, pcsx 440, picodrive 211). Updates risk save-state compatibility. Backups of GBA saves are in `~/powkiddy-rgb20sx/backups`. |
| Moss patches (`moss-patches/`) | The 8 GiB card rule and suspend-mode fix are written, untested. |
| Collections with accented names | Reported earlier, not reproduced. |

## Quick wins (S)

~~1. Screenshot hotkey. Fn+something saves the current frame as a PNG to `Screenshots/<system>/`. Nothing like it exists today; the frame is already in a buffer. Pairs well with Romulus-style sharing later.~~ (done, see FORK.md)
~~2. Left stick as D-pad (with deadzone setting). The RGB20SX has two sticks and NES/GB/SNES games ignore them. A per-system option makes the sticks useful for the games you actually play.~~ (done, see FORK.md)
3. **Theme file.** Colors and fonts are compile-time constants (`TRIAD_*` in `defines.h`). Read them from `.userdata/rgb30/theme.cfg` (and an optional font path) so the look can change without a rebuild. Fun and very visible.
4. **Random game ("Surprise me").** One entry in the launcher that picks an unplayed game from the current folder or collection.
5. "Set time from network" in the Clock tool. Romulus already knows how to bring Wi-Fi up; the Clock tool could fetch the time from the server's HTTP `Date:` header (no NTP client needed) and set it, with the timezone handled properly. Also fixes certificate errors caused by a wrong clock. (not done)
6. **Low-battery protection.** Below ~5%: autosave the state, then shut down cleanly instead of dying mid-write (the atomic saves make this safe now).
7. **Auto power-off after a long sleep.** The device faux-sleeps (suspend). After N hours asleep, wake briefly and shut down so it doesn't drain flat in a bag. Setting file like `sleep-timeout`.
8. Cleaner IP / info tool. Show SSID, signal, IP, battery %, free card space, firmware/commit. Takes an hour and answers "is it on Wi-Fi?" at a glance. (partly: the Wi-Fi tool shows the network name, signal and address)

## Medium (M)

9. **Cheats.** libretro cores (fceumm, snes9x, gambatte, mGBA) accept cheats; MinUI has no UI for them. Read `.cht` files from `Cheats/<system>/`, list them in the in-game menu with on/off toggles, remember per game. High fun factor.
10. **Cover art for the launcher.** The launcher already shows `.res/<rom>.png` next to a game. Add a Romulus action (or a separate tool) that fetches thumbnails for the games on the card, from the libretro-thumbnails GitHub repo by name, or from the server if it ever serves art. Instantly makes the launcher feel finished.
~~11. Wi-Fi tool v2. Scan, pick a network from a list, type the password with the on-screen keyboard (reuse Romulus's), remember several networks, show status. Ends the "edit `wifi.txt` on a computer" step.~~ (done, see FORK.md)
12. **Playtime and stats.** Log start/stop per game; show "Recently played" with hours, a stats screen (most played, streaks, totals). Cheap to log, surprisingly engaging.
~~13. Save backups. Keep the last N versions of each `.sav` (rolling `.bak1/.bak2`) and the autosave state, plus a "restore previous save" menu item. Atomic writes stop corruption; this stops *accidents* (overwritten a good save with a bad one).~~ (done, see FORK.md)
14. **Rewind.** A ring of state snapshots in RAM gives rewind for NES/GB/GBA/SNES. The A55 has headroom; per-system on/off because of memory and CPU.
15. **Crash recovery.** If a core crashes, the device hangs or reboots. Install a signal handler that logs the crash (core, game, backtrace if possible), saves what it can, and returns to the launcher with a message. Makes experimenting with cores painless.
16. **Per-system power profile.** NES/GB don't need the clocks SNES/PSX do. Pick CPU governor/frequency per system from the emulator's real load: longer battery for the games you play most.
17. **Romulus v2.** Resume interrupted downloads (the server supports Range), a queue ("download these 5"), "get everything I'm missing for this platform", filters (verified only, region), show file sizes, and optionally update `.res` art. The API is already read-only and tested.
18. **ROM drop over Wi-Fi.** A tool that serves a tiny upload page on the local network so a phone or laptop can drop ROMs onto the card without taking it out. (Alternative: expose SFTP; OpenSSH, Samba, Syncthing and rclone are all in Moss's package set, though whether they're in your image is untested.)
19. **Bluetooth tool.** Pair headphones or a controller from the device. Moss's package set includes BlueZ and bluez-alsa and PipeWire already routes audio, so it's mostly UI (untested on your image).
20. Self-update tool. Download the latest build of *this fork* (a release on GitHub, or a file on your Romulus server), verify its SHA-256, install with the same backup scheme as `install.sh`. This is the real answer to "an up-to-date fork I can maintain": one tool, no card swapping. (for now replaced by pushing builds over SSH from the Mac, see Tools > SSH)

## Larger (L)

21. **Cross-device saves with the Android app.** Same game, same `.sav`, on the phone and the RGB20SX. Needs a write path on the server (it's read-only today by design), or sync through something that already exists (Syncthing, WebDAV). Worth deciding the server story first.
22. **RetroAchievements.** Hook rcheevos into minarch (needs the network at launch, an account, and a per-core memory map). Big, but it is the feature that makes retro handhelds feel alive.
23. **Own the base image.** Moss boots a full emulation OS, then MinUI. A trimmed image (no EmulationStation, fewer services) would boot faster and use less battery, and removes the PipeWire layer from the audio path entirely if the silent-audio problem turns out to be PipeWire. Risky and device-specific; do it last.
24. **Shaders / LCD effects.** MinUI has simple effect overlays. Proper grid/scanline/color-correct filters for GB/GBA (the screen is square 720x720, so integer-scaled GB looks great with a pixel grid).

## Housekeeping that protects the work

- **CI.** Run `tools/host-tests/run.sh` and the Docker device build on every push (GitHub Actions or a local script). The tests already exist; nothing runs them automatically.
- **Pin and mirror dependencies.** Every dependency is pinned to a commit, but if an upstream repo disappears the build breaks. Mirror them (or vendor tarballs) in your own repo/releases.
- **Release process.** Tag, build, write `SHA256SUMS`, attach `test-package/` as a release. `install.sh`/`revert.sh` already do the hard part.
- **Move `moss-patches` forward.** Build Moss once with them so they stop being "untested".
- **Fix the device-side logging noise.** Every key press and stick movement is logged with `LOG_info`; at the volume the logs reach (the PKM log is 200 KB) that costs I/O during play. Quiet by default, a `debug-log` flag file to turn it on.

## What I would do first

1. **Screenshot hotkey, stick-as-D-pad, theme file** (S, S, S): immediate daily-use value, low risk.
2. **Self-update tool** (M): it removes the card-swapping step from every future change, which is what you've been doing all session.
3. **Cheats** (M) and **cover art** (M): the two most noticeable "this is a better OS" features.
4. **Wi-Fi tool v2** (M): needed anyway for travelling with Romulus.
5. Keep collecting evidence for the **silent audio** problem; if PipeWire is confirmed, bypassing it for ALSA directly is a small change with a big effect.

## What I need from you to go further
- The next time audio goes silent: power off without launching anything else, card to the Mac (logs are already there).
- On the device: `date`, `echo $TZ`, and `hwclock` output (for the clock fix), and whether rumble works in any game.
- A decision on the server story for saves/art: is changing the romulus server (writes, art) on the table, or should everything stay read-only?
