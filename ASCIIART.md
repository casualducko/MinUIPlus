# How the ASCIIArt tiles are made

Tiles are 360 x 360 PNGs (a cartridge front) made by `workspace/all/titleart/titleart_gen.c` (layout and wrapping logic in `titleart_core.c`, tested on the Mac by `tools/host-tests/test_titleart*.c`). The launcher draws them in the game list; the ASCIIArt tool, Romulus (a game just downloaded) and the boot pass all use the same generator.

## The pipeline
1. **Title**: the file name without tags and extension (`Legend of Zelda, The (USA).sfc` is `The Legend of Zelda`), split at ` - ` or `: ` into a main title and a subtitle.
2. **Layout** (`TA_layout`): the main title is wrapped onto up to 4 lines and the letter height (in 12 px rows) chosen so the whole title fits in 50 columns x 22 rows. The widths come from rendering each line in the big font (`rasterLine`, 96 px) and measuring it; `CONDENSE` (0.9) makes the letters a little narrower than the font's own shape so long words get taller letters.
3. **Raster to a grid**: each line's coverage is shrunk with a box filter (`shrink`) onto the grid the look needs.
4. **Look** (below).
5. The subtitle, the `[ SYSTEM ]` tag and the cartridge outline (`drawCart`) are added; colors come from the theme (`theme.white` bright, `theme.dark_text` dim). Titles that do not fit in letters (a word too long for 4-row letters in 50 columns, like RETRANSLATED, a title too long, or Japanese/Chinese) are drawn as plain characters from the small cell font, 3 times its size if the whole title fits that way, else 2 times, else 1 (never cut off).

## The looks (`.userdata/rgb30/titleart-style`)
| Value | What | Notes |
|---|---|---|
| `classic` (default) | The original look: cells 6 x 12 px, each filled with one of three hand-drawn marks thresholded at 42%: an `H` for a full cell and its upper or lower half for a half cell. (The marks were 1 px bars, a short dash and a dot; on 2026-10-25 they became 2 px bars that fill the cell's height with the crossbar kept, so strokes join up: S, O, B, 2 and 4 no longer smear at 4 rows. Tried and dropped: cutting each letter separately from a thin font, which left the strokes too thin for marks.) | Letters at least 4 rows high; subtitles are plain double-size characters. Restored as the default on 2026-10-25 after the other looks were tried and rejected on the device (`ascii` looked bad; `solid` looked nice but was not ASCII). |
| `ascii` | Real ASCII art: cells 4 x 8 px, each filled with one of ` .:+#@` by how much the letter covers it (`powf(coverage, 0.7)`, nothing below 20%). All characters share one box (the ink of a `#`), so edges are light marks and bodies heavy ones. | Letters at least 4 rows high; shorter titles, and subtitles, are plain double-size characters. Tried 2026-10-25 and rejected by the user on the device. Chosen over the other sizes after comparing cells of 3x6, 4x8 and 5x10 px and natural vs stretched glyphs: 4 x 8 with shared-box glyphs was the clearest that still reads as characters. |
| `solid` | The same grid drawn as solid 3 px square dots that run together into clean letters. | The clearest of all (the first rewrite, commit `649381b`; the user liked how it looked but wanted something that is really ASCII); letters 3 rows or more, subtitle in letters too. Looks like a normal bold font. |
| `dots` | `solid` with a 1 px gap between the dots (dot matrix). | Dimmer; may shimmer on the panel. |

Existing tiles are not changed when the style changes: ASCIIArt tool, X (Recreate all). Art you added yourself is never replaced.

## What the first (original) look got wrong
Each filled 6 x 12 px cell was one of three hand-drawn marks (an "H" for a full cell, a dash for the top half, a dot for the bottom half), thresholded at 42%. Strokes were one cell wide, every filled cell carried the H texture, and letters were 4 to 5 rows high, so curves broke up and long titles were unreadable. Fixes, in order: solid dots on a 3 px grid (commit `649381b`), then characters on a finer 4 x 8 grid with a weight ramp (this look). Also fixed along the way: plain-text characters wider than their cell (M, W) are squeezed to fit instead of cut off.

## Trying a new look
`TA_layout` and `composeTile` decide where letters go; a look is a function that draws one line (`drawLetters` picks it from the style). Generate sample tiles on the Mac with `RES=<res folder> tools/host-harness/titleart-run.sh "wait,X,sleep:3000,A,sleep:8000,snap:done"` (tiles land in the fake card's `Roms/*/.res`); put `printf '%s' ascii` in the card's `.userdata/macos/titleart-style` to try another look.
