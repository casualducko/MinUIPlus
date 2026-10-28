# Notice: what is whose

## Based on MinUI
This repository is a fork of [MinUI](https://github.com/shauninman/MinUI) by Shaun Inman. MinUI's
repository has no license file, so no license is granted here for MinUI's own code, and the
changes this fork makes inside MinUI's files are not licensed by this repository either. The
upstream README is kept as `UPSTREAM-README.md`.

What the author has said: MinUI's README once had a "Developers" section (removed in commit f1d0341,
2023-09-23, with the note "does this feel antagonistic? not the intent") that read, in short:
not soliciting source contributions; feel free to fork or learn from any of the repos, but there is no
time to manage a community or be managed by one. This fork is made on that understanding, the same
one other public MinUI forks (for example NextUI) rely on. It is a statement of intent, not a license,
and no reply to a request for one is expected, so the maintainer of this fork has decided to treat it
as permission to fork, modify and share the builds (including the update package and any card image
that carries MinUI's base). If the author wants anything changed or taken down, say so in an issue
here and it will be done. This fork is not affiliated with or endorsed by the author, and it is
for non-commercial use: do not sell it. If you want to reuse MinUI's code in something else, ask upstream.

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
- **Moss** (the operating system this runs on; JELOS's Apache-2.0 software, GPL parts such as the Linux kernel, and branding under CC BY-NC-SA, so non-commercial use only) is a separate project and its source is not part of this repository. `moss/` holds our changes to it (two small patches to its scripts, a one-line patch to the Linux kernel's RGB30 device tree, and device tree binaries built from that tree, which is GPL-2.0 kernel code): those follow Moss's and the kernel's licenses, not this repository's MIT license.

## Not included
No games, BIOS files or other copyrighted game data are in this repository or its releases.
