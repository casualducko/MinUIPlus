# Ideas for the RGB20SX fork

Last refreshed 2026-10-24 (late night). `FORK.md` lists what has been done in detail; this is the state of the fork and what could come next. Sizes are rough: **S** is an evening, **M** a few evenings, **L** a project. "Device" means it needs the unit in hand to finish.

## Where things stand

**What the fork has now**

| Area | What exists |
|---|---|
| Launcher and games | Fixed volume keys, Fn as MENU, left stick as D-pad (deadzone setting), numbered save slot picker, Restore Save (versions of every save), screenshots (MENU+SELECT), margins and big-row options pages, timezone fix |
| Audio | Rate control (NES crackle gone), no pop at game start, audio buffer setting, volume/brightness pill in games |
| Power | Sleep and power-off timeouts, low-battery shutdown, Tools > Settings |
| Cheats | Options > Cheats from `.cht` files, **download per game** from the libretro database (exact name, then the closest title), remembered per game |
| Stats | Play time counted in minarch, Tools > Stats (most played / recent) |
| Look | `theme.cfg` (colors, background, font), **Tools > Theme** (presets, palette, live preview, save), long names readable (wider selected row, Y shows the whole line) |
| Network | Tools > Wi-Fi v2 (scan, password, several saved networks), Tools > SSH, Tools > Romulus (browse, download, **play straight from the "Downloaded" screen**) |
| Safety nets | Atomic saves, crash reporter, frame-timing and audio diagnostics, installer with backups, host tests (about 25 suites) and Mac harnesses |

**Confirmed on the device by you or by files on the card:** volume keys, NES audio, screenshots, slot picker, Settings, Wi-Fi v2 on the home network, cheat download (a `Mega Man (USA).cht` is on the card), play time recorded (three games in `playtime.txt`).

**Built and tested on the Mac only, not yet confirmed on the device:** Tools > Theme and the background color, Romulus "play now", the Y key for long names, Restore Save, low-battery shutdown, Romulus end to end.

### What we learned that is not in the code
- **The RGB20SX has no rumble motor.** The Settings tool says so.
- **The CPU is stuck at 1.416 GHz.** Its voltage regulator (TCS4525, I2C 0x1c) never binds to a driver, so `cpufreq` never starts. Fixing it means changing Moss's device tree on TF1; not attempted.
- **The Zelda dropouts were menu time and game start**, not slowness. The pot-break sound is in all three SNES cores' output (cause unknown, dropped).
- **mGBA updates** `517e518` and `925f0f0` both segfaulted on the device; the old core is back.
- Sysfs files `stat` as regular files; writing them needs a plain write.
- A tool cannot queue a game through `/tmp/next`: `MinUI.pak/launch.sh` deletes it when the tool returns. The tool's own `launch.sh` has to run the command.
- The corner icons (Wi-Fi, battery) come from the image sheet in white, so a very light theme background hides them.

### Still open
| Item | Notes |
|---|---|
| Silent audio now and then | Cause unknown, not seen since the diagnostics went in. If it recurs: power off, take the card, read `.userdata/rgb30/logs/<system>.txt`. |
| Core updates (mGBA etc.) | Cores are pinned to Nov 2024 commits. Updates risk save-state compatibility. |
| Moss patches (`moss-patches/`) | The 8 GiB card rule and suspend-mode fix are written, untested. |
| Wi-Fi away from home | Only the home network has been seen so far. |
| Japanese and Chinese titles | The font has no such glyphs, so they show blank (Stats, launcher). |

## What is missing, ranked

The ones I would do first are at the top of each size.

### Small (S)
1. **Turbo buttons.** Rapid-fire A/B/X/Y per game, toggled in Options > Controls or by a hotkey. Standard on handhelds; MinUI has nothing. Small change in the input path.
2. **Set the clock from the network.** The Clock tool fetches the time from the HTTP `Date:` header of any HTTPS site and sets it (zone already handled). The battery-less clock drifts and a wrong clock breaks TLS.
3. **Random game ("Surprise me").** One launcher entry that picks an unplayed game from the current folder or collection (play time now tells us which are unplayed).
4. **System info tool.** Battery %, storage free, Wi-Fi name and IP, build hash, uptime, in one screen. Answers "is it on Wi-Fi?" and "which build is this?" at a glance.
5. **Screenshot gallery.** Browse `Screenshots/` on the device, delete, and show one as the launcher background or a game's cover.
6. **Quiet logging.** Every key press is logged; a `debug-log` flag file turns it on, off by default (less I/O during play).
7. **Cheat page polish.** Re-download, delete a cheat file, and "enable all" from the cheat list.

### Medium (M)
8. **Self-update tool.** Tools > Update checks your GitHub repo's latest release, downloads the build, verifies SHA-256, installs it with the same backups as `install.sh`. Ends card swapping and the Mac-to-device pushes for anyone using the fork. Needs a public release (or a token in a config file) for the repo; it is private now.
9. **Add to collection from a game.** MinUI collections are text files; a menu item in Options (and in the launcher) adds or removes the current game. Today it takes a computer.
10. **Rewind.** A ring of state snapshots in RAM for NES/GB/GBA/SNES, per-system on/off.
11. **Save backup and sync.** Copy saves to a zip on the card on a schedule, or sync to a server (Romulus server write path, or WebDAV/Syncthing which Moss already ships). The only protection against a lost card today is Restore Save on the same card.
12. **ROM drop over Wi-Fi.** A tool that serves a small upload page so a phone or laptop can send ROMs and covers without taking the card out.
13. **Bluetooth tool.** Pair headphones or a controller (BlueZ and PipeWire are in Moss; untested on this image). Needs the device.
14. **CJK fallback font.** A second font used for glyphs the first lacks, so Japanese titles are readable.
15. **Per-system power profile.** NES/GB don't need the clocks SNES/PSX do, but the CPU can't change speed on this unit (see above), so this waits for the regulator fix.
16. **Romulus v2.** Resume interrupted downloads, a queue, "get everything I'm missing", filters, sizes. Now that PLAY exists, "download and play" is already there for single games.
17. **In-game extras menu.** Fast-forward speed, sound mute, brightness shortcuts already exist; a single page for them would be quicker than digging through Options.

### Large (L)
18. **Fix CPU scaling.** A Moss DTB change so `cpufreq` binds and MinUI's CPU settings do something; battery life would improve. Touches TF1, so it needs a tested, reversible image.
19. **RetroAchievements.** rcheevos hooked into minarch (network at launch, account, memory maps). Big, but the feature that makes handhelds feel alive.
20. **Core manager.** Download newer or extra cores from a build server, check save-state compatibility, keep the old one. Solves the stuck-at-2024 cores, but each core has to be tested.
21. **Own the base image.** A trimmed Moss without EmulationStation and the extra services: faster boot, less battery, fewer audio layers.
22. **Shaders and LCD effects.** Proper grid/scanline/colour-correct filters for GB/GBA on the square panel.
23. **Netplay.** Link-cable style play over Wi-Fi.
24. **Cover art.** Parked by you ("not doing cover art for MinUI right now"); the Romulus/libretro-thumbnails idea still stands if it comes back.

## Housekeeping that protects the work
- **CI.** Run `tools/host-tests/run.sh` and the Docker build on every push to the new GitHub repo (GitHub Actions or a local hook). The tests exist; nothing runs them automatically.
- **Releases.** Tag, build, attach `test-package/` with `SHA256SUMS` as a release (also what the self-update tool would read).
- **Pin and mirror dependencies.** Every dependency is pinned to a commit; mirror them so a deleted upstream cannot break the build.
- **Move `moss-patches` forward.** Build Moss once with them so they stop being untested.
- **Trim the repo.** Upstream's other platforms are still in the tree; fine, but a README at the top saying this is the RGB20SX fork would help anyone who finds the repo.

## What I would do next
1. **Self-update tool (8)**, now that the repo exists: it removes the card-swapping step from every future change.
2. **Turbo (1), clock from the network (2), system info (4)**: each a short evening, each used daily.
3. **Add to collection (9)** and **random game (3)** for the launcher.
4. Keep the **silent-audio** diagnostics in place and **test Wi-Fi away from home**.

## What I need from you to go further
- Whether the repo can have a **public release** (for the updater), or a token file is acceptable.
- Which of Theme, Cheats, Stats, Romulus "play now" and long-name (Y) misbehave on the device, with a note of what you saw.
- The next time audio goes silent: power off without launching anything else, card to the Mac (logs are already there).
