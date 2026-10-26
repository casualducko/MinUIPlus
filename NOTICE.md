# Notice: what is whose

## Based on MinUI
This repository is a fork of [MinUI](https://github.com/shauninman/MinUI) by Shaun Inman. MinUI's
repository has no license file, so no license is granted here for MinUI's own code, and the
changes this fork makes inside MinUI's files are not licensed by this repository either. If you
want to reuse MinUI code, ask upstream. The upstream README is kept as `UPSTREAM-README.md`.

## Original to this fork (MIT, see `LICENSE`)
Folders and files that did not exist in MinUI:
- `workspace/all/update/`, `info/`, `gallery/`, `stats/`, `theme/`, `titleart/`, `romulus/`, `pico/`, `switcher/`, `wifi/`, `power/` (the tools: Update, Info, Screenshots, Stats, Themes, ASCIIArt, Romulus, PicoSearch, Switcher, Wi-Fi, Settings)
- `skeleton/EXTRAS/Tools/rgb30/` launch scripts for those tools
- `docs/` (the web installer)
- `tools/` (installer and release scripts, host tests, Mac harnesses, ASCII-art helpers)
- `moss/` (the scripts, notes and the one-line change to the RGB30 device tree), `MOSS.md`, `MOSS-CPU.md`, `ASCIIART.md`
- `FORK.md`, `IDEAS.md`, `README.md`, `NOTICE.md`

## Third-party software that is shipped or built
- **Libretro cores** (mGBA, Gambatte, FCEUmm, Snes9x and others, built from pinned commits): each under its own license (GPL, MPL and others, some non-commercial). See each core's repository.
- **Noto Sans JP** (subset used as the fallback font): SIL Open Font License; the license text is `skeleton/SYSTEM/res/fallback-OFL.txt`.
- **SDL, SDL_image, SDL_ttf, sdl12-compat, curl, BusyBox and the Moss/JELOS system** on the device: their own licenses.
- **Moss** (the operating system this runs on) is a separate project and its source is not part of this repository. `moss/` holds our changes to it (two small patches to its scripts, a one-line patch to the Linux kernel's RGB30 device tree, and device tree binaries built from that tree, which is GPL-2.0 kernel code): those follow Moss's and the kernel's licenses, not this repository's MIT license.

## Not included
No games, BIOS files or other copyrighted game data are in this repository or its releases.
