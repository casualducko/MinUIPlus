# This fork

A maintained fork of [MinUI](https://github.com/shauninman/MinUI) (archived
2025-11-27 at `dbf8943`) for the Powkiddy **RGB20SX** / **RGB30**, running on
[Moss](https://github.com/shauninman/Moss). It keeps the `rgb30` platform name
and is a drop-in for an existing MinUI card: replace the files in
`.system/rgb30/bin/` (and `lib/`, `cores/`) and nothing else changes.

Why the platform stays `rgb30`: MinUI keys `.system/`, `.userdata/`, `Emus/` and
`Tools/` by platform name, so renaming it hides a card's extras and settings.
The RGB20SX's panel is the RGB30's 720x720, so there is nothing to split.

## Building

Needs Docker. `make PLATFORM=rgb30 build` (run it under `script` or a terminal,
the toolchain container is started with `-it`).

Every dependency is pinned to the commit the shipped release was built from (its
`.system/commits.txt`): the 13 cores (`*_HASH` in `workspace/rgb30/cores/makefile`),
the toolchain, `libretro-common` and DinguxCommander. The cores can't float to
upstream HEAD because Shaun's patches no longer apply there. From a clean tree the
build reproduces the 20251127-1 binaries byte for byte, except for embedded build
timestamps. Write the short hash you want in the version string to
`workspace/hash.txt` first (it is gitignored).

## Tests

- `tools/host-tests/run.sh`: regression tests for the fixes below, with
  AddressSanitizer. No device or SDL needed.
- `tools/host-harness/run.sh`: runs the real launcher on a Mac against a fake SD
  card with scripted input and screenshots (`make-stress-card.sh` adds hostile content).
- `tools/host-tests/run_core_harness.sh`: runs a real libretro core through the real
  audio code on aarch64 with the device toolchain (needs Docker, a core and a game).

## Changes from upstream

| | |
|---|---|
| keymon | volume and brightness keys were reversed (`KEY_VOLUMEDOWN` is 114, it treated that as plus). `swap-volume-keys` in `.userdata/rgb30` restores the old direction |
| minui | a folder name with an unclosed `(` crashed the launcher at startup |
| minui | listing a system no longer shows ROMs from folders that have no emu pak (SFC next to SUPA) |
| minui | the alt tag (`(USA)`, `(GBC)`) survives when a long name is truncated |
| minui | a ROM with a long name overflowed the stack when listed or launched (256 byte path buffers, now `MAX_PATH` 1024) |
| minui | leaked a surface per duplicate-named row per redraw; the selected row could show a `001) ` sort prefix |
| minarch | MENU+button no longer also sends the plain button to the game; binding the same MENU+button twice is refused |
| audio | NES/SNES crackle: frames are paced at ~60.0 fps but the core makes audio for 60.0988, so the buffer drained (measured: 2759 of 8465 callbacks ran dry). Dynamic rate control now nudges the playback rate (~0.14%, max 1%) to keep the buffer level. Also: the underrun fill paired left with the previous frame's right; underruns are counted and logged on exit (`audio:` line) |
| api | `GFX_truncateText` wrote before its buffer; `GFX_blitMessage` overflowed on long rows and dropped messages of 16+ rows |
| clock | cursor could land on a non-existent AM/PM field in 24 hour mode (uninitialized width) |
| minarch | `State_read`/`State_write` closed an uninitialized `FILE*` if the first allocation failed; a core option without a description crashed the options menu; undefined `1 << -1` for unbound buttons |
| api | `GFX_wrapText` wrote through NULL when the first word was wider than the room (e.g. a core description starting with a URL); `allocFile` and `hasCollections` didn't check for failure |
| minui | paths built from text read out of `recent.txt`, collections, `.m3u` files or the auto-resume file overflowed the stack for a line near the buffer size (found by running the launcher on a stress card) |
| minarch | the core's tag (from a folder name in parentheses), version and extension list were `strcpy`'d into fixed fields of `struct Core`, a core with more than 31 extensions or one longer than 6 characters overflowed the zip handling, `Core_getName` crashed on a core with no underscore in its file name, `main` crashed with no arguments |
| msettings | a corrupt or short `msettings.bin` could apply a volume of 9999% or leave the brightness uninitialized; now clamped, a short file means defaults |
| audio | the buffer target is a third of the buffer (was a quarter): fewer underruns and less back-and-forth sample dropping for 7 ms more latency; each game start logs the mixer state, and `no-audio-rate-control` in `.userdata/rgb30` turns the rate control off for comparison |
| Wi-Fi.pak | a `wifi.txt` saved on Windows (CRLF) left a `\r` on the network name and password, so it never connected |

Everything above is covered by a host test or the harness except what needs the
hardware, which is marked "not tested on hardware" in its commit message: the
volume key direction, the audio change, and the minarch input change.

## Still open

- **Audio crackle**: the cause is fixed in the simulation (`tools/host-tests/test_audio_sim.c`), not yet confirmed by ear on the device. The `audio:` line in the logs says how many underruns are left.
- **Clock set in MinUI is offset by the timezone after a reboot.** Moss defaults to
  UTC and MinUI has no timezone setting; needs `date`/`hwclock` readings from the
  device to pin down.
- **Collections can't handle accented characters** (probably Unicode normalization
  between a text file and the filesystem); not reproducible on macOS.
- Moss itself: `moss-patches/` next to this repo has two untested patches (restart
  logind when the sleep config changes; allow 8 GB cards).

## Fade-in and in-game volume pill
- Audio fade-in (`SND_fadeIn` in api.c): the first 50 ms of a game's sound ramps up from silence, so a core that starts at a non-zero level (NES) no longer steps from 0 to that level, which clicks. Counts from the first non-silent frame. Host test: `test_fadein.c`. Only helps if the pop is a step; a pop from the sound chip powering up is not touched.
- In-game volume/brightness pill: keymon changes the settings in another process, so minarch now watches `GetVolume()`/`GetBrightness()` per frame (`Hud_update` in minarch.c) and shows the menu's pill for 1 s. The game is drawn at its own size straight from the core's buffer, so the pill is rendered at device size (`GFX_renderSettingHud`, drawn on white and on black to recover alpha) and composited by the platform (`PLAT_setHud`, rgb30 `PLAT_flip`). Other platforms get a no-op (weak default). Not shown on HDMI. `tools/host-harness/hud-test.c` renders it over a checkerboard for a visual check.

## Fn button
- The Fn button sends evdev code 316 (BTN_MODE), the only code in the device logs nothing handled. It now acts as MENU like L3 and R3: `RAW_MENU3` in rgb30 platform.c and `CODE_MENU_FN` in keymon (so Fn + volume changes brightness). Inferred from the logs, not from a key test: if Fn turns out to send another code, the launcher/minarch log line `key event: N` shows it.

## Romulus tool (new)
A Tools pak that browses a romulus server (the API described in ROMULUS_HANDOFF.md from the Android app) and downloads ROMs into the right Roms folder. Source: `workspace/all/romulus/` (`json.c` parser, `zipx.c` zip extraction with zlib, `rapi.c` config/URLs/curl job/naming/placement, `romulus.c` the UI), pak launcher in `skeleton/EXTRAS/Tools/rgb30/Romulus.pak/`.
- Config: `.userdata/rgb30/romulus.cfg` on the card (`url=https://...`, `token=...`, optional `map=slug:Folder (TAG)`); created empty on first launch. https only (`insecure_http=1` allows http for testing).
- Network via the device's `curl` (run in the background, token in a private header file, never on the command line); The app checks the network itself at launch: if Wi-Fi is up with a default route it goes straight in; if it's off (or wlan0 is missing) it runs `wifictl enable` and waits (up to 30 s) for the link and an address, showing "Turning on Wi-Fi..." / "Waiting for the network..."; if the first request still fails to resolve/connect right after it switched Wi-Fi on, it quietly retries 3 times, 2 s apart. If Wi-Fi is already up but has no address yet it waits without touching it. Platforms the server reports are listed when this device has an emulator for them; folder = the existing `Name (TAG)` folder (GBA goes to `(MGBA)` when that's what the card has), else it makes one.
- A: download (asks first if you already have it), X or Fn: search (on-screen keyboard), B: back, L1/R1: page. Zips are unpacked (first file the emulator takes, base name only, CRC checked); odd file names are cleaned for FAT; a download never leaves a partial ROM.
- Tests: `tools/host-tests/test_romulus.c` (+ `mock_romulus.py`, `make_zips.py`) run in `run.sh` under ASan/UBSan; `tools/host-harness/romulus-run.sh` drives the real UI against the mock server with screenshots.
- **Play right after a download.** If the system has an emulator on the card, the "Downloaded" screen stays until a button is pressed: A = PLAY, B = BACK (without an emulator it is the old 3 second message). PLAY writes the same command the launcher would run (`'<emulator>/launch.sh' '<rom>'`, single-quote escaped; checked with names containing `'`, `$()` and backticks) to `/tmp/romulus-play`, sets the resume slot to the hidden default so no old state resumes, and quits. It cannot go through `/tmp/next`, because `MinUI.pak/launch.sh` deletes that file as soon as a tool returns, so `Romulus.pak/launch.sh` runs the command itself after `romulus.elf` exits; when the game ends, the launcher comes back. Rendered on the Mac harness; not tried on the device.
- Not tried on the device or against the real server.

## Autosleep timeout
- Idle autosleep was a fixed 30 seconds (`SLEEP_DELAY` in `PWR_update`). It is now 120 seconds by default, and `.userdata/rgb30/sleep-timeout` (a number of seconds on one line) overrides it: `0` = never, otherwise 10 to 86400, anything else is ignored. Read once at start and logged (`autosleep after N seconds`). Applies wherever `PWR_update` runs (launcher, tools, the in-game menu); sleeping on a long download is still prevented by the tools. Test: `test_sleeptimeout.c`.

## Overnight review (2026-10-23/24)
Found and fixed, each with a host test where it can have one:
- **Multi-disc launching was broken by my own earlier change**: `getFirstDisc` bounded its output with `sizeof(disc_path)`, a pointer parameter, so the disc path was cut to 7 characters (an .m3u game would launch with a garbage path, auto-resume of one too). Now `MAX_PATH`. `test_firstdisc.c` fails on the old code.
- **`getFile` left the caller's buffer untouched when the file was missing**, so callers read uninitialized stack. Now an empty string. Together with an empty-string TTF render (returns NULL) this crashed the launcher's version screen on a card without `.system/version.txt`; the screen now shows "unknown". `test_getfile.c`, `test_version.c`.
- **minarch ran `mkdir -p "<tag>-<core>"` through a shell**, where the tag is a ROM folder name: a folder with a backtick or `$()` in its tag ran as a command. Now a shell-free `mkdirs()` in utils.c (`test_mkdirs.c` checks that hostile names only make folders). Also: `Game_open` and `main` no longer `strcpy` unbounded paths / `strrchr()+1` without a slash; the relative disc path joins in minui and minarch are bounded.
- Romulus: network readiness is "has a default route" (any interface), `LAST_PATH` macro clash, dead variable, per-row folder scans cached, stale download temp removed at start, copy checks read errors, child processes close inherited fds.
- Dead code: unused variables in clock, minput, say, `PWR_update`; `ox`/`oy` initialized.
- Checked and found clean: clang static analyzer and GCC `-fanalyzer` on the new code, macOS `leaks` on the launcher (repeated folder navigation) and on Romulus (browse, search, paging, three downloads, a cancel): 0 leaks.
- **Saves could be destroyed by a failed save.** `State_write`, `SRAM_write`, `RTC_write` and `Config_write` opened the file with `"w"` (which empties it) *before* asking the core for the data or writing anything: a core that failed to serialize, a full card, or a power cut during the write left an empty save in place of the old one. They now serialize first and replace the file in one step (`writeFileAtomic` in utils.c: write `file.tmp`, flush, fsync, rename over). `State_read` also refuses a zero-byte state instead of feeding the core a buffer of zeros. `test_atomicwrite.c`.
- The launcher's recents list (`recent.txt`) and every `putFile` (resume slot, flags) are replaced atomically too.

## putFile regression (found 2026-10-24)
The atomic-write change made `putFile` write `<path>.tmp` and rename. That cannot work on sysfs, so `bl_power` (the sleep backlight) and the CPU governor writes silently failed in builds `527dac4` and `b081bea`: the screen stayed lit during sleep until the 2-minute power-off. `putFile` now writes in place for anything that isn't a regular file (test in `test_atomicwrite.c`). Brightness was unaffected (msettings writes it itself).
The first fix tested `S_ISREG` and did not work: sysfs attributes look like regular files to `stat()`. The test is now by path prefix (`/sys/`, `/proc/`, `/dev/`) as well.

## Screenshot hotkey, stick deadzone
- **MENU+SELECT** (Fn+SELECT, L3/R3+SELECT) in a game saves the game's own picture (unscaled, no menu or pill) as a PNG to `/Screenshots/<system>/<game> <date-time>.png` and shows a "Screenshot saved" pill. Written as `.part` then renamed. It is the one shortcut that comes bound; rebind or clear it in the in-game menu under Shortcuts (Restore Defaults brings it back).
- **Left stick:** in "Standard" controller mode the left stick already counts as the D-pad (the launcher and minarch have always done this). The threshold is now `.userdata/rgb30/stick-deadzone` (percent of travel, 10 to 90, default 40, was a fixed 50). Fixed `just_repeated &= neg` (should be `&= ~neg`) in `PAD_setAnalog`, which cleared every other direction's repeat when a stick returned to centre. The per-axis log line is off (it logged every stick sample).

## Sleep & Power tool
`Tools > Sleep & Power` (`workspace/all/power`) sets `.userdata/rgb30/sleep-timeout` (idle seconds before sleep, default 120) and the new `.userdata/rgb30/poweroff-timeout` (seconds asleep before powering off, default 120, never while charging). 0 means never for both; the same strict parsing as before (bad file = default). Read once when the launcher or a game starts. Games still never sleep on their own (original MinUI behavior); the power button always sleeps. Host harness: `tools/host-harness/power-run.sh`.
Screenshots are scaled up by a whole number so the longest side is at most 1080 px (GB 960x864, GBA 960x640, NES 1024x960), nearest-neighbor.

## SSH tool
`Tools > SSH` (`skeleton/EXTRAS/Tools/rgb30/SSH.pak`) runs `systemctl start sshd` (only until reboot), appends `authorized_key.pub` (if present beside it; the test package ships the Mac's dedicated key, not the repo) to `/storage/.ssh/authorized_keys`, and shows the wlan0 address. Log in as root with `ssh -i ~/.ssh/rgb20sx_ed25519 root@<address>`. Untested on the device.

## Audio buffer setting
`.userdata/rgb30/audio-buffer` (milliseconds, 20 to 500, nothing or 0 = default about 28 ms) sets how much sound is kept ready. The buffer grows to three times that (the rate control aims for a third of it) and starts with the cushion already in place. A bigger cushion covers occasional busy frames (simulated: a 45 ms busy frame every 15 s drops sound with the default and not with 80); it cannot cover sustained slowness, because the rate control can only win back about 1% of the time. Set it in Tools > Power & Audio (the tool was "Sleep & Power"). Evidence from the device: Zelda on snes9x2005_plus starved in bursts, CPU fixed at 1.416 GHz (the TCS4525 CPU regulator doesn't bind, so cpufreq never starts).

## mGBA core update
Tried `517e518` (3 Aug 2026, 817 commits past the old pin): **crashed on the device while loading a GBA game** (segfault right after `game path`, minarch log in `Emus/rgb30/MGBA.pak`; no kernel record of the address). The last two commits (`d030c2a` libretro VFS/BIOS, `517e518` audio resample) are days old and rework the libretro layer, so the pin is now `925f0f0` (2 Aug 2026, the merge before them; libretro layer as of May 2026). It is the last-but-two commit that still has `Makefile.libretro` (upstream moved libretro to CMake right after). The patch applies unchanged. Save states are core-version specific: back up `.userdata/shared/MGBA-mgba` and `Saves/` first (backup in `~/powkiddy-rgb20sx/backups`). The previous core (sha256 0c63f769...) stays beside it as `mgba_libretro.so.old-2024` for a one-line revert. Not yet tested on the device.

## Crash reporter (minarch)
A fatal signal (SEGV, BUS, ABRT, FPE, ILL) now writes to the game's log: `CRASH: signal 0xb`, the address touched, `pc` and `lr` as `module+offset`, and a backtrace, then the process dies as before. Use `addr2line -f -e <unstripped core or minarch.elf> <offset>` (aarch64 toolchain in the docker image) on the offsets. Added after the new mGBA cores segfaulted on the device with no way to see where.

## audio-dump diagnostic
If `.userdata/<platform>/audio-dump` exists, minarch writes the core's raw audio (S16 stereo, the core's rate, before resampling or rate control) to `/tmp/audio-dump-<rate>.raw`, up to 40 MB (about 5 minutes). Used to tell whether a sound that is not heard was ever produced (Zelda pot-break noise missing with both snes9x2005_plus and Supafaust, clean timing and counters).

## Save versions and low-battery shutdown
- **Save versions (minarch):** `SRAM_write` no longer rewrites an unchanged save; when the save changes, the version it replaces is copied to `<Saves>/<system>/.backups/<game>.sav.1` (newest) to `.3` (oldest), keeping its modification time (so the label says when it was saved). A version is kept only if it was written at least 10 minutes after the newest kept one, so three versions reach back over sessions, not menu visits. Options > **Restore Save** lists them ("Save from Oct 24, 13:14"), confirms, keeps what the game has now as the newest older version, puts the chosen one in the game and resets it (the game reads its save at start). SRAM only: states, RTC and the auto-resume state are not versioned. Helpers `fileEquals`/`backupRotate` in utils.c, tests `test_backup.c` and `test_sram.c`.
- **Low-battery shutdown (api.c):** `PLAT_getBatteryPercent()` (weak default -1; rgb30 reads `/sys/class/power_supply/battery/capacity`). Below `.userdata/<platform>/low-battery-shutdown` percent (default 5, 0 = off, max 20), not charging, on 3 readings 5 s apart (2 readings 30 s apart while asleep), after a 60 s grace after start: the app's before-sleep hook runs (minarch: SRAM, RTC, autosave state, auto-resume) and the device powers off with "Battery empty, quicksave created, powering off". Set in Tools > Power & Audio ("Low battery shutdown").

## Slot picker in the save and load screens
The row under the state preview used to be eight dots, with no numbers and nothing saying left/right changes the slot. It is now `< 1 2 3 4 5 6 7 8 >` (`GFX_blitSlotPicker` in api.c, drawn by minarch's menu): the current slot in a dark block, slots that already hold a state in black, empty ones in gray. The row is taller (`SLOT_PICKER_HEIGHT` 14). Rendered on the Mac by `tools/host-harness/hud-test.c` (slots-*.bmp); not yet seen on the device.

## Margins and slot picker polish
- `PADDING` in `workspace/rgb30/platform/platform.h` was 40 (x2 = 80 px on each edge, about 11% of the 720 px panel) as in upstream's RGB30 setup; now 20 (40 px). Affects every screen (launcher, in-game menu, tools, the volume pill). Before/after launcher renders: `evidence/padding-compare/`. Harness: `PADDING=20 tools/host-harness/run.sh ...`.
- Slot picker: larger numbers (`font.large`), a round dark capsule for the current slot (`GFX_fillCapsule`), `SLOT_PICKER_HEIGHT` 18.
- In-game **Options** (and its Save Changes / Restore Save lists, type MENU_LIST) now use the main menu's big rows (`font.large`, `PILL_SIZE` pills, left aligned) instead of the 20-unit small ones; the description line at the bottom uses `font.small`. The detail pages (Frontend, Emulator, Controls, Shortcuts: MENU_VAR/MENU_INPUT/MENU_FIXED) now use the same big rows: name on a white pill when selected, a dark gray pill behind the whole row, the value at the right in `font.medium` (names are cut at 60% of the width, values to the room left). 8 rows fit on the 720 px panel, longer pages scroll as before. Rendered by `tools/host-harness/hud-test.c` (options-list, options-page); not yet seen on the device.

## Wi-Fi tool v2
`Tools > Wi-Fi` (`workspace/all/wifi`, `Wi-Fi.pak`): Wi-Fi on/off, **Networks nearby** (scan; signal bars and a padlock; connect to a saved or open one directly, type a password on a keyboard for a new one, "Other network..." for a name you type), **Saved networks** (several; connect, or X to forget), a status line (name, signal, address).
- Moss keeps one network (`wifi.ssid` and `wifi.key` settings, connman): choosing a network runs `wifictl enable <name> <password>` (it accepts both on the command line), sets those two settings and, once the link is up with a route, remembers it in `.userdata/rgb30/wifi-networks.txt` (name, tab, password; newest first). A failed connection puts the previous network back.
- Scanning: `connmanctl scan wifi`, then `iw dev wlan0 scan dump` (strength, security); if that shows nothing, `connmanctl services` (names and security only). Status: `iw dev wlan0 link`, `ip`. Commands run through `fork`/`execvp` with an argument array, never a shell, so names and passwords with quotes or spaces are safe.
- The first time, the network in the old tool's `wifi.txt` is imported. The earlier tool stays as `launch-old.sh` and runs if `wifi.elf` is missing.
- Keyboard: lower, upper, symbols pages (X), show/hide (Y), B deletes (or backs out when empty), START finishes; up to 63 characters.
- Tests: `tools/host-tests/test_wifi.c` (iw and connman output, the saved file, commands), and the real tool against fake commands: `tools/host-harness/wifi-run.sh` (`mock-wifi/`). Not yet run on the device: the real `iw` output could differ from the samples; if the list is empty or odd, check `iw dev wlan0 scan dump`.
- Limits: a hidden network ("Other network...") only works if connman auto-provisions it by name; names with spaces can fail wifictl's own explicit connect step (connman's AutoConnect should still join); no WEP; switching networks drops an SSH session.

## Rumble
`PLAT_setRumble` was an empty stub. Now `SetRawRumble()` in libmsettings (rgb30) drives the motor: the first input device that supports force feedback (`FF_RUMBLE`, how the joypad driver exposes its PWM motor) via `EVIOCSFF` plus an `EV_FF` play event (effect length 60 s, restarted when the strength changes, stopped with 0); if there is none, Moss's PWM (`/sys/class/pwm/pwmchip1/pwm0`, set up at boot as period 1 ms with duty = period meaning off) is driven directly, but only if that exact setup is already there (period 1000000 and enabled), so the backlight's PWM can't be touched by a wrong guess. `VIB_quit` now sets the motor off itself (the thread could be cancelled before it applied the 0). Strength setting: `.userdata/rgb30/rumble` (0 to 100 percent of what the game asks, default 100), in Tools > Settings (the tool was Power & Audio), which buzzes briefly when you change it and says whether a motor was found. The game log says `rumble: <backend>` the first time a core rumbles. **Not run on the device**: the driver details come from the Moss RK3399 joypad patch (same driver family) and the RGB30 quirk script; if the note says "No rumble motor found", check `cat /proc/bus/input/devices` for a device whose `B: EV=` has bit 0x15 set (force feedback) and `ls /sys/class/pwm`.

**Rumble result on the device (2026-10-24):** no force feedback device; Moss's PWM exists and is configured as expected (period 1000000, inverse polarity, enabled), but driving it directly (duty 0, 300000, 600000 inverse; 1000000 and 500000 normal polarity, 1.5 to 2 s each) produced no vibration, and Drill Dozer at 100% did not rumble. The device tree has no motor or rumble property on the joypad node. Conclusion: this RGB20SX most likely has no vibration motor (the DTB is the RGB30's). The code is kept (harmless, and it would work on a unit with a motor); the Settings tool now says "The RGB20SX has no rumble motor."

## Theme file

Colors and the font are no longer compile-time only. `.userdata/rgb30/theme.cfg` (read when the launcher, a game or a tool starts) can contain lines like

```
# a warm theme
white=ffd98c
dark_gray=2a1f14
font=MyFont.ttf
```

Names: `white black light_gray gray dark_gray light_text dark_text button_text` (RRGGBB, with or without `#` or `0x`) and `font` (a .ttf/.otf in the same folder, or an absolute path; a font that fails to open falls back to the default with a log line). `white`, `black`, `light_gray`, `gray` and `dark_gray` also recolor the pills, bars and other tinted assets (they are drawn in these colors). Unknown names and bad values are ignored. `COLOR_*` in `defines.h` now read from the `theme` struct in `api.c`; `THEME_parse` is covered by `test_theme.c`. Builds and passes the host tests; **not yet seen on the device**.

**Theme tool and the background color.** `theme.cfg` now also takes `background=RRGGBB` (the color behind the launcher, menus and tools; default black). It is used by `GFX_clearUI`, which every UI screen calls instead of `GFX_clear`; the two clears around game video stay black so game borders don't change. `GFX_applyTheme()` remaps the colors and recolors a freshly loaded asset sheet, so a program can change `theme` and see it at once. Tools > **Theme** (`workspace/all/theme/`, `Theme.pak`) uses that: a Preset row (Stock, Warm, Amber, Ocean, Forest, Violet; Left/Right), the nine colors with a swatch and hex each, **Save and apply**, **Reset to stock**. A color opens an editor drawn in the colors being edited: Up/Down pick red, green or blue, Left/Right change by 4, L1/R1 by 32, A opens a palette of 32 colors, X undoes, Y goes to the stock color, B returns. Nothing is written until Save; leaving with unsaved changes asks first. Save writes every color (and keeps an existing `font=` line) through `THEME_format`, reads it back to check, then leaves so the launcher starts again with the new colors. `THEME_parse`, `THEME_parseColor` and `THEME_format` are now public (tests in `test_theme.c`, including format-then-parse). The Mac harness is `tools/host-harness/theme-run.sh` (`L1`/`R1` tokens added to the script syntax). Limits: the wifi and battery icons in the corner are drawn from the image sheet in white, so a very light background hides them; game screens are not themed. Not tried on the device.

## Title art (Tools > TitleArt) and the art switch
The launcher already draws `<Roms folder>/.res/<file name>.png` at the right edge for the selected game. **Tools > TitleArt** makes that picture for every game that has none: the game's title in big ASCII letters, a subtitle (after " - " or ": ") in small plain text, a rule and a `[ SYSTEM ]` badge, 360x360 with a transparent background, in the theme's colors (`white` for the letters, `dark_text` for the dim ends; remake after a theme change with X). Which tiles are the tool's own is kept in `.userdata/rgb30/titleart-made.txt` (`mtime<TAB>path`); art that was already there, or that replaced a tile later (the mtime no longer matches), is never touched or remade. A makes the missing ones (one per frame, B stops), X makes the tool's own again, Y switches the launcher's game art on or off.
- **How a tile is made.** `TA_title` cleans the name (tags and extension dropped, ", The" moved to the front), `TA_split` takes the subtitle, `TA_layout` tries one to four lines of the title and keeps the one with the tallest letters (4 rows is the least; words are dropped from the end of a very long title), each line is drawn in the UI font at 96 px, shrunk with a box filter to two sub-pixels per row, thresholded, and packed into characters: `H` for both halves of a cell, `'` for the top, `.` for the bottom (`TA_pack`). The tile is 60 x 30 cells of 6 x 12 px; the three fill characters are drawn from bitmaps in the tool (so they always fit a cell and don't depend on a font), the small text uses the UI font centered in its cell.
- **Names next to the art.** While a picture shows, every row of the launcher list, the selected one too, is shortened to stop before it (the stock launcher let the selected pill run under the picture). The picture carries the whole title anyway.
- **A broken picture no longer crashes the launcher.** `IMG_Load` returning NULL (a `.res/*.png` that is not a picture, a half-copied file) was dereferenced; it is now treated as no picture. Found with a fake art file on the Mac card.
- **New games get art.** (1) Romulus: right after a download is saved, if the game art is on and the game has none, `TAGen_forRom` makes its picture, so a new download is never without one (nothing is made when the art is off). (2) Boot pass: Tools > Settings > "Art for new games" = Automatic (`.userdata/rgb30/titleart-auto` = 1; default Manual) makes `MinUI.pak/launch.sh` run `titleart.elf --auto` once at boot, before the launcher: no screen, theme colors read from `theme.cfg`, only games without art, at most 60 tiles or 4 seconds per boot (the next boot carries on), nothing when the art is off; the log is `logs/titleart.txt`. Games added over SSH or by changing the card appear at the next boot; for a big first batch use Tools > TitleArt (A) instead. The generator is shared: `titleart_gen.c` (Title Art tool, Romulus, boot pass). Every tile made is appended to `titleart-made.txt` (the last line for a path wins), which is how X and a remake know which tiles are the tool's own.
- **The switch.** `.userdata/rgb30/show-art` holding `0` hides the launcher's game art (the launcher reads it each time it draws, so no restart); anything else or no file shows it. Tools > **Settings** has a "Game art" On/Off row; Tools > TitleArt has Y. It hides all art in the launcher, the tool's tiles and your own alike.
- Code: `workspace/all/titleart/` (`titleart_core.c` has no graphics and is covered by `tools/host-tests/test_titleart.c`: names, splitting, wrapping, layout, packing, badges; `titleart.c` is the tool). The Mac harness `tools/host-harness/titleart-run.sh` runs the real tool on a fake card (including a user's own tile that must stay). The look was prototyped in `tools/asciiart/prototype.py` (same logic; the C tiles have the same character pattern). Not tried on the device.

## Cheats

In a game: Menu > Options > **Cheats** lists the cheats for that game, each Off/On (left/right or A). They're read from a RetroArch-style `.cht` file:

```
cheats = 2
cheat0_desc = "Infinite Lives"
cheat0_code = "SXIOPO"
cheat0_enable = false
cheat1_desc = "Max Coins"
cheat1_code = "AAA+BBB"
cheat1_enable = false
```

Put it at `Cheats/<system tag>/<game>.cht` on the card (`<system tag>` is the part in parentheses in the Roms folder name, eg. `Cheats/FC/Bobl.cht` for `Roms/NES (FC)/Bobl.nes`; both `Bobl.nes.cht` and `Bobl.cht` are found). The codes are whatever the core understands (Game Genie / Pro Action Replay / GameShark, `+` joins several), passed straight to `retro_cheat_set`. The `.cht` files of the libretro-database project use this format (their names must be changed to match your ROM's name). Which cheats are on is remembered per game in `.userdata/rgb30/<system>-<core>/<game file>.cheats` (`index<TAB>description` lines); once that file exists it wins over the `cheatN_enable` flags. The game log says `cheats: N from <file>, M on`. Without a file the page says where to put one. Code: `Cheat_parse`, `Cheat_readState`, `Cheat_writeState` (covered by `test_cheats.c`), `Cheats_load/apply`, `OptionCheats_*` in `minarch.c`. Every core in the build exports `retro_cheat_set`. `tools/host-tests/run_cheat_harness.sh` runs a real core on aarch64 and checks the effect: with the pinned fceumm core, `retro_cheat_set(0, true, "0011:5A")` makes system RAM `0011` read `5A` and `retro_cheat_reset` lets it go (checked 2026-10-24). **Not yet tried in a game on the device** (nobody has a .cht on the card yet; try a code on a NES game).

**Long names.** In an options page with values (Cheats, Emulator, Frontend...), the selected row's name may use all the room up to its value, and when it is still cut short the line at the bottom says "Press Y to read the whole line"; Y shows the full name and the description (for a cheat, its code). Code: `selected_cut` and the Y handler in `Menu_options`. Not tried on the device.

**Download per game.** With no `.cht` for the game, Options > Cheats asks "Download them from the libretro cheat database?" (A downloads, B goes back). It uses the same files RetroArch's online updater fetches (`libretro-database/cht/<system folder>/<name>.cht` on GitHub). Steps: bring Wi-Fi up with `wifictl enable` if `/proc/net/route` has no default route (waits up to 40 s); `curl` the file named like the ROM; if that is a 404 (curl exit 22), `curl` the folder's listing from the GitHub tree API (about 140 KB compressed for GBA, 2262 names for NES) and take the closest name; save it as `Cheats/<tag>/<game>.cht`, load it and open the list. B cancels at any point; errors say what happened (no Wi-Fi, can't reach GitHub, took too long, nothing found). Code: `Cheatdb_*` in `utils.c` (system tag to database folder, URL encoding, listing parser, matching; `test_cheatdb.c`) and `Net_*` / `Cheats_download` in `minarch.c`. Matching ignores the `(region)` and `[tag]` parts, case and punctuation, moves ", The" to the front, then scores same region, World/USA/Europe, and fewer extra tags (Rev, Action Replay, Game Genie) and avoids Beta/Proto/Demo. Only a title that matches is taken, never a guess: renamed ROMs, hacks and homebrew will usually find nothing. The system tags with a database folder are in `cheatdb_folders` (FC GB SGB GBC GBA SFC SUPA SNX MD SMS GG PS PCE NGP NGPC VB and some more). Checked against the real NES listing on the Mac and `curl` (TLS, `--compressed`, 404 exit 22) on the device itself; the in-game flow is not yet tried on the device.


## Play time and the Stats tool

minarch counts how long a game actually runs: the time between frames, ignoring any gap over a second (the menu, sleep, a long load), so fast-forward counts as real time. A launch is recorded when the game starts; the time is added to `.userdata/shared/.minui/playtime.txt` every five minutes of play and when the game closes. Format, one line per game: `seconds<TAB>launches<TAB>last played (unix time)<TAB>ROM path` (`Playtime_*` in `utils.c`, covered by `test_playtime.c`). A hard power loss costs at most the last five minutes.

**Tools > Stats** (`workspace/all/stats`) lists games by time played with the total on top: Up/Down scroll, L/R page, X switches between most played and recently played, and the line under the list shows how often the selected game was started and when it was last played. Names are the ROM's file name without folders, extension or (tags). It only knows play time from this version on. The tool was rendered on the Mac with sample data (`tools/host-harness/stats-run.sh`); **not yet run on the device**. The font has no CJK glyphs, so such titles show blank.
