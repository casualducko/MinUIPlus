# Host-side regression tests

Small tests that run on a normal computer, with AddressSanitizer and UBSan on,
for bugs that were fixed in this fork. Run them with:

    tools/host-tests/run.sh

Each test compiles the **real function out of `workspace/all`** (see
`extract.py`), not a copy, so it always tests the current source and a
regression shows up as a failure. They need a C compiler and python3. No SDL, no
device.

| test | covers |
|---|---|
| `test_emuname.c` | `getEmuName()` no longer crashes on a folder name with an unclosed `(` |
| `test_underrun.c` | audio underrun fill keeps left/right together; counters |
| `test_truncate.c` | `GFX_truncateText()` underflow and UTF-8 cuts, `GFX_truncateUniqueText()` keeps the alt tag |
| `test_wraptext.c` | `GFX_wrapText()` still wraps normal text exactly as the original (golden file) and survives a too-wide first word |
| `test_blitmessage.c` | `GFX_blitMessage()` with a 1000 character row, 20 rows, a failed render |
| `test_inputclaim.c` | minarch: MENU+button claims its button |

`core_harness.c` / `run_core_harness.sh` run a real libretro core (eg. fceumm from a card) through the real
audio code on aarch64 with the device toolchain, to check what the audio thread would get.
It is not part of `run.sh` because it needs Docker and a core and a game.

What these can't cover is anything that needs the hardware (audio crackle, the
volume key direction, the timezone offset). `tools/host-harness` runs the real
launcher UI on a Mac for the visual behavior.

To add a test, extract the function with `extract.py FILE START END`, stub what
it calls, and add the name to the loop in `run.sh`.
