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
