# Running the real launcher on a Mac

`run.sh` builds MinUI's launcher (`workspace/all/minui`) natively with the
`macos` dummy platform, points it at a fake SD card, feeds it scripted button
presses and saves screenshots of what it draws. It is for checking list
behavior (which systems and ROMs are shown, how long names are truncated,
crashes on odd file names) without a device. See also `workspace/macos/notes.txt`.

    tools/host-harness/run.sh "snap:root,D,A,snap:system"

Needs macOS, `brew install sdl2 sdl2_image sdl2_ttf`, and a copy of a MinUI
card's `.system/res` folder (fonts and images), given as `RES=/path/to/.system/res`.
The screenshots land in `$OUT` (default `/tmp/minui-host-harness/shots`) as PNG.

## Script language

Comma separated steps, run one every few frames:

| step | meaning |
|---|---|
| `U` `D` `L` `R` | d-pad |
| `A` `B` `X` `Y` | face buttons |
| `snap:name` | save `name.png` of what is on screen |
| `wait` | do nothing for a step |

The script ends the program when it runs out. Launching a ROM ends it too.

## The fake card

`make-card.sh` builds one in `$CARD` with systems that exercise the awkward
cases: a system folder with no emu pak, an unclosed `(` in a folder name, duplicate
titles with long names, a 200 character file name, an accented name, an empty
folder. The folder layout is `Roms/<System> (TAG)/` and an emu pak exists for
`SFC`, `GB`, `GBC` and `FC` only.

## Gotchas

- AddressSanitizer binaries that link Homebrew's SDL2 hang at startup on at least
  some macOS versions, so this build is not sanitized. The tests in
  `tools/host-tests` are, because they don't link SDL.
- MinUI remembers the last launched ROM in `/tmp/last.txt` and reopens there;
  `run.sh` clears it first.
- `api.c` has 32 bit ARM assembly guarded only by `__ARM_ARCH >= 5`; the build
  pretends to be an older ARM so clang on Apple silicon takes the C path.
- `clock.c` uses GCC nested functions, so the clock tool can't be built with clang.
