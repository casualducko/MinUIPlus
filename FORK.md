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
  card with scripted input and screenshots.

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
| audio | the underrun fill paired the left channel with the previous frame's right; underruns are now counted and logged on exit |
| api | `GFX_truncateText` wrote before its buffer; `GFX_blitMessage` overflowed on long rows and dropped messages of 16+ rows |
| clock | cursor could land on a non-existent AM/PM field in 24 hour mode (uninitialized width) |

Everything above is covered by a host test or the harness except what needs the
hardware, which is marked "not tested on hardware" in its commit message: the
volume key direction, the audio change, and the minarch input change.

## Still open

- **Audio crackle** (fceumm, snes9x2005_plus). The underrun counters are there to
  measure it: play something, quit, read the `audio:` line in the logs.
- **Clock set in MinUI is offset by the timezone after a reboot.** Moss defaults to
  UTC and MinUI has no timezone setting; needs `date`/`hwclock` readings from the
  device to pin down.
- **Collections can't handle accented characters** (probably Unicode normalization
  between a text file and the filesystem); not reproducible on macOS.
- Moss itself: `moss-patches/` next to this repo has two untested patches (restart
  logind when the sleep config changes; allow 8 GB cards).
