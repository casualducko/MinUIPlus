# Generated ASCII box art (prototype)

`prototype.py` (Python + Pillow, macOS fonts) shows what generated art could look like before it is
written in C for the device. Nothing here runs on the device.

- `python3 prototype.py sheet out.png [stock|warm|ocean]` — contact sheet for 25 real game names
- `python3 prototype.py one "Name (USA).gba" GBA out.png [style 0-3] [theme]`
- `python3 prototype.py image shot.png "Name" GBA out.png` — art made from a screenshot
- `python3 prototype.py mock out.png` — a tile placed on the launcher list screenshot

The art is a pure function of the title (tags and extension dropped, ", The" moved to the front): the
same game always gets the same art, and `(USA)` / `(Europe)` copies match. The seed is FNV-1a of the
lowercased title, the random numbers xorshift32, so a C port gives the same pictures.
Styles: 0 creature (mirrored blob), 1 plasma, 2 landscape, 3 monogram (the title's first letter).
Tiles are 360x360 with a 40x20 grid of 9x18 px cells, which is what the launcher blits at the right
edge from `.res/<rom file name>.png`.
