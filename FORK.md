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
