# Ideas for the RGB20SX fork

Written overnight 2026-10-23/24 after reviewing the code. `FORK.md` lists what has been done; this is what could come next. Sizes are rough: **S** is an evening, **M** a few evenings, **L** a project. "Device" means it needs the unit in hand to finish.

## Where things stand

Working and confirmed on the device: volume direction, NES audio (rate control), no pop at game start (fade-in), volume/brightness pill in games, Fn as MENU, longer autosleep, menus.
Built and tested only on this Mac (not yet heard on the device): Romulus tool, the overnight fixes below.

### Fixed in the review (details in `FORK.md`)
- Multi-disc (.m3u) games would have launched with a garbage path: a regression I introduced earlier (`sizeof` of a pointer). Now has a test that fails on the old code.
- A missing `version.txt` crashed the launcher's version screen (`getFile` left uninitialized memory, then an empty string went to the text renderer).
- **A failed save could destroy the previous one.** States, cartridge saves, RTC, config and recents were opened with `"w"` before the data was ready. They are now written to a temp file and renamed over the old one.
- minarch ran `mkdir -p "<folder tag>..."` through a shell: a ROM folder named with a backtick or `$()` ran as a command. Now plain `mkdir`.
- Several unbounded path copies are bounded; dead code and unused variables removed; Romulus tidied (network check by any interface, cached folder lookups, leftover temp files).
- Clean: clang analyzer and GCC `-fanalyzer` on the new code, and 0 leaks (macOS `leaks`) in the launcher and Romulus after long scripted sessions.

### Still open (known, unexplained or untested)
| Item | Notes |
|---|---|
| Silent audio now and then | Cause unknown. Buffers and counters look healthy during silence. Evidence so far: the sound card's stream is owned by PipeWire (not by the emulator), so the likely culprit is PipeWire's routing/sleep. The next silent session will log the chip path and PipeWire state (diagnostics are already in). |
| Clock off by a timezone after reboot | Hypothesis: the Clock tool runs `date -s` without the timezone Moss applies at boot, so the stored time is read back shifted. Needs `date`, `hwclock`, `echo $TZ` readings from the device to confirm. |
| Wi-Fi tool handles one network | `wifi.txt` holds a single SSID/password. See "Wi-Fi tool" below. |
| Rumble | `PLAT_setRumble` is an empty stub although Moss sets up a motor. Device. |
| Collections with accented names, snes9x save-state compatibility | Reported earlier, not reproduced. |
| Moss patches (`moss-patches/`) | The 8 GiB card rule and suspend-mode fix are written but need a Moss build to test. |

## Quick wins (S)

1. **Screenshot hotkey.** Fn+something saves the current frame as a PNG to `Screenshots/<system>/`. Nothing like it exists today; the frame is already in a buffer. Pairs well with Romulus-style sharing later.
2. **Left stick as D-pad (with deadzone setting).** The RGB20SX has two sticks and NES/GB/SNES games ignore them. A per-system option makes the sticks useful for the games you actually play.
3. **Theme file.** Colors and fonts are compile-time constants (`TRIAD_*` in `defines.h`). Read them from `.userdata/rgb30/theme.cfg` (and an optional font path) so the look can change without a rebuild. Fun and very visible.
4. **Random game ("Surprise me").** One entry in the launcher that picks an unplayed game from the current folder or collection.
5. **"Set time from network" in the Clock tool.** Romulus already knows how to bring Wi-Fi up; the Clock tool could fetch the time from the server's HTTP `Date:` header (no NTP client needed) and set it, with the timezone handled properly. Also fixes certificate errors caused by a wrong clock.
6. **Low-battery protection.** Below ~5%: autosave the state, then shut down cleanly instead of dying mid-write (the atomic saves make this safe now).
7. **Auto power-off after a long sleep.** The device faux-sleeps (suspend). After N hours asleep, wake briefly and shut down so it doesn't drain flat in a bag. Setting file like `sleep-timeout`.
8. **Cleaner IP / info tool.** Show SSID, signal, IP, battery %, free card space, firmware/commit. Takes an hour and answers "is it on Wi-Fi?" at a glance.

## Medium (M)

9. **Cheats.** libretro cores (fceumm, snes9x, gambatte, mGBA) accept cheats; MinUI has no UI for them. Read `.cht` files from `Cheats/<system>/`, list them in the in-game menu with on/off toggles, remember per game. High fun factor.
10. **Cover art for the launcher.** The launcher already shows `.res/<rom>.png` next to a game. Add a Romulus action (or a separate tool) that fetches thumbnails for the games on the card, from the libretro-thumbnails GitHub repo by name, or from the server if it ever serves art. Instantly makes the launcher feel finished.
11. **Wi-Fi tool v2.** Scan, pick a network from a list, type the password with the on-screen keyboard (reuse Romulus's), remember several networks, show status. Ends the "edit `wifi.txt` on a computer" step.
12. **Playtime and stats.** Log start/stop per game; show "Recently played" with hours, a stats screen (most played, streaks, totals). Cheap to log, surprisingly engaging.
13. **Save backups.** Keep the last N versions of each `.sav` (rolling `.bak1/.bak2`) and the autosave state, plus a "restore previous save" menu item. Atomic writes stop corruption; this stops *accidents* (overwritten a good save with a bad one).
14. **Rewind.** A ring of state snapshots in RAM gives rewind for NES/GB/GBA/SNES. The A55 has headroom; per-system on/off because of memory and CPU.
15. **Crash recovery.** If a core crashes, the device hangs or reboots. Install a signal handler that logs the crash (core, game, backtrace if possible), saves what it can, and returns to the launcher with a message. Makes experimenting with cores painless.
16. **Per-system power profile.** NES/GB don't need the clocks SNES/PSX do. Pick CPU governor/frequency per system from the emulator's real load: longer battery for the games you play most.
17. **Romulus v2.** Resume interrupted downloads (the server supports Range), a queue ("download these 5"), "get everything I'm missing for this platform", filters (verified only, region), show file sizes, and optionally update `.res` art. The API is already read-only and tested.
18. **ROM drop over Wi-Fi.** A tool that serves a tiny upload page on the local network so a phone or laptop can drop ROMs onto the card without taking it out. (Alternative: expose SFTP; OpenSSH, Samba, Syncthing and rclone are all in Moss's package set, though whether they're in your image is untested.)
19. **Bluetooth tool.** Pair headphones or a controller from the device. Moss's package set includes BlueZ and bluez-alsa and PipeWire already routes audio, so it's mostly UI (untested on your image).
20. **Self-update tool.** Download the latest build of *this fork* (a release on GitHub, or a file on your Romulus server), verify its SHA-256, install with the same backup scheme as `install.sh`. This is the real answer to "an up-to-date fork I can maintain": one tool, no card swapping.

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

## What I'd do first

1. **Screenshot hotkey, stick-as-D-pad, theme file** (S, S, S): immediate daily-use value, low risk.
2. **Self-update tool** (M): it removes the card-swapping step from every future change, which is what you've been doing all session.
3. **Cheats** (M) and **cover art** (M): the two most noticeable "this is a better OS" features.
4. **Wi-Fi tool v2** (M): needed anyway for travelling with Romulus.
5. Keep collecting evidence for the **silent audio** problem; if PipeWire is confirmed, bypassing it for ALSA directly is a small change with a big effect.

## What I need from you to go further
- The next time audio goes silent: power off without launching anything else, card to the Mac (logs are already there).
- On the device: `date`, `echo $TZ`, and `hwclock` output (for the clock fix), and whether rumble works in any game.
- A decision on the server story for saves/art: is changing the romulus server (writes, art) on the table, or should everything stay read-only?
