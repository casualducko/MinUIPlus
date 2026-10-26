# Ideas for the RGB20SX fork

Last refreshed 2026-10-26 (after a long day of work). `FORK.md` lists what has been done in detail; this is the state of the fork and what could come next. Sizes are rough: **S** is an evening, **M** a few evenings, **L** a project. "Device" means it needs the unit in hand to finish.

## Where things stand

**What the fork has now**

| Area | What exists |
|---|---|
| Launcher and games | Fixed volume keys, left stick as D-pad (deadzone setting), numbered save slot picker, Restore Save (versions of every save), screenshots (MENU+SELECT), margins and big-row options pages, timezone fix, **9 home rows**, **SELECT: Tools**, **START: a game's page (collections, delete with a confirmation)**, **New game dot** (also for native PICO-8), optional clock and an SSH dot beside the Wi-Fi icon |
| Switching | **Tools > Switcher on Fn** (recent games as cards with their last save state, A resumes), **auto-save on quit** (option), Info is Tools > Info |
| Audio | Rate control (NES crackle gone), no pop at game start, audio buffer setting, volume/brightness pill in games |
| Power | Sleep and power-off timeouts, low-battery shutdown, Tools > Settings |
| Cheats | Options > Cheats from `.cht` files, **download per game** from the libretro database (exact name, then the closest title), remembered per game |
| Stats | Play time counted in minarch, Tools > Stats (most played / recent) |
| Look | `theme.cfg`, **Tools > Themes** (16 presets, palette, live preview; the corner icons follow the text color), long names readable (wider selected row, Y shows the whole line) |
| Launcher | **Y: random game** (never-played first; at the root, in a random folder), selected name **scrolls** when it doesn't fit, duplicate-title tags readable, game art on/off, **Tools > ASCIIArt** (cartridge tiles in three looks; also for PICO-8 carts; Romulus and PicoSearch make one per download, optional boot pass, MAKE NEW / REDO ALL) |
| Text | **Japanese titles** (fallback font, Noto Sans JP subset) everywhere names are shown |
| Tools | **Info**, **Screenshots** gallery, **Clock** sets itself from the network (Y), **Stats** (reset with Y), **Update** (from GitHub releases), **Settings** (rows alphabetical, 7 visible) |
| In a game | **Turbo A/B/X/Y/L/R** with a speed, in Options > Frontend |
| Network | Tools > Wi-Fi v2 (scan, password, several saved networks), Tools > SSH, Tools > Romulus (browse, download, play straight from the "Downloaded" screen, **Y: random game**), **Tools > PicoSearch** (the Lexaloffle BBS: ranked search, Featured, Popular now, tags, Random; downloads into the PICO-8 folder), a **web installer** (GitHub Pages) |
| Moss (the OS on TF1) | **CPU frequency scaling works** (the unit has a SYR827 regulator, not the TCS4525 the RGB30 tree names; patched device tree in `moss/`), the model name can be set (`model.txt`) |
| Safety nets | Atomic saves, crash reporter, frame-timing and audio diagnostics, installer with backups, releases with checksums, host tests (about 30 suites) and Mac harnesses for every tool |

**Confirmed on the device by you or by files on the card:** volume keys, NES audio, screenshots, slot picker, Settings, Wi-Fi v2 on the home network, cheat download, play time recorded, the clock, Tools > Update, Themes, ASCIIArt (the classic look), PicoSearch (search, download, play), CPU scaling and sleep/wake with it, GBA with the original core.

**Built and tested on the Mac only, not yet confirmed on the device:** the Switcher (Fn), auto-save on quit, delete game, the PicoSearch browse menu and details strip, Romulus's random game, Restore Save, low-battery shutdown, the SSH dot after its fix.

### What we learned that is not in the code
- **The RGB20SX has no rumble motor.** The Settings tool says so.
- **The CPU was stuck at 1.416 GHz until 2026-10-25.** Moss's RGB30 device tree names a TCS4525 regulator at I2C 0x1c; the RGB20SX has a SYR827-class one at 0x40, so the driver never bound and `cpufreq` never started. Fixed by a two-line device tree change written to `/flash` (see `MOSS-CPU.md`, `moss/`); a Moss update would undo it until the kernel patch `moss/0004` is built into an image.
- **The boot logo is in the kernel image** (the first boot script inside `/flash/KERNEL` draws it before anything is mounted), so no tool on the card can change it (see `MOSS.md`).
- **Quitting a game saved nothing** until auto-save on quit; only sleep and power-off auto-saved (slot 9).
- **The Zelda dropouts were menu time and game start**, not slowness. The pot-break sound is in all three SNES cores' output (cause unknown, dropped).
- **mGBA updates** `517e518` and `925f0f0` both segfault on the device (in `retro_serialize_size`, as the game starts). The original core (Nov 2024, `747362c`) is back and in the package; the releases `.1003` to `.2127` shipped the bad one and say so in their notes.
- Sysfs files `stat` as regular files; writing them needs a plain write.
- A tool cannot queue a game through `/tmp/next`: `MinUI.pak/launch.sh` deletes it when the tool returns. The tool's own `launch.sh` has to run the command.
- The corner icons (Wi-Fi, battery) come from the image sheet in white, so a very light theme background hides them.

### Still open
| Item | Notes |
|---|---|
| Silent audio now and then | Cause unknown, not seen since the diagnostics went in. If it recurs: power off, take the card, read `.userdata/rgb30/logs/<system>.txt`. |
| Core updates (mGBA etc.) | Cores are pinned to Nov 2024 commits. Updates risk save-state compatibility. |
| Moss patches (`moss/`) | The 8 GiB card rule and the suspend-mode fix are written, untested; the kernel patch for the CPU fix is checked against the pinned kernel source, no image built from it. |
| Wi-Fi away from home | Only the home network has been seen so far. |
| Chinese and Korean titles | The fallback font is the Japanese subset only, so hanzi and hangul show blank (see S5). |

## What is missing, ranked

The ones I would do first are at the top of each size.

### Small (S)
1. ~~Quiet logging~~ done: a `debug-log` flag file turns key logging on.
2. **Cheat page polish.** Re-download, delete a cheat file, and "enable all" from the cheat list.

3. ~~A "new" mark~~ done: a dot beside unplayed games (Settings > New game dot).
4. ~~Bigger plain-text title tiles~~ done: double size when the title fits.
5. **Chinese and Korean in the fallback font.** The subset is JIS (Japanese) only; a bigger subset built the same way (a few MB more) covers hanzi and hangul.
6. **Per-game turbo / a hotkey.** Turbo is a frontend option now (saved per game with Save Changes); a shortcut to toggle it without opening the menu.

### Medium (M)
8. ~~Self-update tool~~ done: Tools > Update, releases on GitHub (`tools/package/release.sh <package> --publish`) and a web installer.
9. ~~Add to collection from a game~~ done: START in the launcher, Options > Collections in a game.
10. **Rewind.** A ring of state snapshots in RAM for NES/GB/GBA/SNES, per-system on/off.
11. **Save backup and sync.** Copy saves to a zip on the card on a schedule, or sync to a server (Romulus server write path, or WebDAV/Syncthing which Moss already ships). The only protection against a lost card today is Restore Save on the same card.
12. **ROM drop over Wi-Fi.** A tool that serves a small upload page so a phone or laptop can send ROMs and covers without taking the card out.
13. **Bluetooth tool.** Pair headphones or a controller (BlueZ and PipeWire are in Moss; untested on this image). Needs the device.
15. **Per-system power profile.** NES/GB don't need the clocks SNES/PSX do. The CPU can change speed now (2026-10-25), and the CPU Speed option per game works; a learned per-game speed is idea 1 in the new ideas below.
16. **Romulus v2.** Resume interrupted downloads, a queue, "get everything I'm missing", filters, sizes. "Download and play" and a random game already exist.
17. **In-game extras menu.** Fast-forward speed, sound mute, brightness shortcuts already exist; a single page for them would be quicker than digging through Options.

### Large (L)
18. ~~Fix CPU scaling~~ done (device tree on `/flash`, `moss/apply-dtb.sh`); what is left is putting it in a Moss build so an update keeps it.
19. **RetroAchievements.** rcheevos hooked into minarch (network at launch, account, memory maps). Big, but the feature that makes handhelds feel alive.
20. **Core manager.** Download newer or extra cores from a build server, check save-state compatibility, keep the old one. Solves the stuck-at-2024 cores, but each core has to be tested.
21. **Own the base image.** A trimmed Moss without EmulationStation and the extra services: faster boot, less battery, fewer audio layers.
22. **Shaders and LCD effects.** Proper grid/scanline/colour-correct filters for GB/GBA on the square panel.
23. **Netplay.** Link-cable style play over Wi-Fi.
24. **Cover art.** Parked by you ("not doing cover art for MinUI right now"); the Romulus/libretro-thumbnails idea still stands if it comes back.

## New ideas: things other custom firmwares do not do (brainstormed 2026-10-26)

Mostly built on what this fork already has (working CPU scaling, play records, save-state pictures, the cartridge art, PicoSearch's BBS client, the Switcher). "Not done elsewhere" is my belief from the docs of Onion, muOS, NextUI/MinUI, ArkOS, JELOS, Spruce and KNULLI; I could not check every release of every firmware, so treat it as "I have not seen it", not a proof.

**Make the hardware work smarter (needs the CPU scaling fix, done 2026-10-25)**
1. **Learned CPU speed per game (M).** Today a game runs at the speed its CPU Speed option says. The frame-timing diagnostics already exist in minarch: let it find, per game, the lowest step (408 to 1992 MHz) that keeps the frame time under budget, remember it in the game's config, and re-check now and then. Battery life rises with no setting to touch.
2. **Battery coach (S/M).** Log the battery drop per minute per system and per game; the Info page and the Switcher card then say "about 4 h 20 min left at this game's usual draw". Needs a small log in `.userdata` and the existing playtime hooks.
3. **Thermal guard (S).** The SoC temperature is readable; if a long session passes a limit, step the clock down a notch (and say so) before the unit gets uncomfortable in the hand.
4. **Crash-proof sessions (M).** A rolling auto-save every few minutes while a game runs (the Quit and sleep auto-save already exist), so a dead battery or a crash costs at most a few minutes: next start offers "Resume from 3 minutes ago".

**Make the library smarter**
5. **Library doctor (M).** One tool that finds duplicate ROMs (same checksum, different names), ROMs with no emulator, orphaned saves, states and title art (the delete function leaves saves on purpose), names that clash, and offers to clean up with a preview first.
6. **Smart collections (M).** Collections that fill themselves: Unplayed, Most played, Short sessions, Recently added, Same series (by name), plus rules you write in a text file. The play records and the New dot logic are most of the plumbing.
7. **Backlog and notes (S).** Mark a game To play, Playing, Beaten, Shelved, and keep a one-line note ("password: 4F2A", "stuck at the second boss"); shown on the Switcher card and in the START page. A notebook for the library is something no firmware seems to have.
8. **Fuzzy search across the whole card (S/M).** One search box over every ROM: typo tolerant, accent and kana blind, finds "zelda" in "Legend of Zelda, The". PicoSearch's ranking code (`bbs_match_tier`) is the start.
9. **Profiles (M).** Several people share one card, each with their own saves, states, playtime, recents and collections, switched with one menu entry (a different `.userdata` root and `Saves` folder). Onion's Guest Mode only hides things; real per-person save sets is the useful version.

**Make play more fun (new ground)**
10. **The ASCII filter (M).** A video filter that draws the live game as characters (the ASCIIArt ramp, run per frame). Pointless and delightful, and fitting for a fork whose boot art is ASCII. Needs the cell font drawn fast enough for 60 fps (a pre-rendered glyph atlas, nearest sampling).
11. **Play journal and year in review (S/M).** A calendar of the days you played (a heatmap in ASCII), streaks, your most played five, "this month you played 11 h across 6 systems", all from `playtime.txt` plus a day log. A tool, a screen you want to open on Sunday.
12. **Own achievements (S).** Small badges for the handheld itself ("7 days in a row", "played on all systems", "finished a game under an hour of notes"). Local, offline, silly, no server like RetroAchievements needs.
13. **Roulette and game night (S).** Random game with a timer ("5 minutes, best score wins"), a pass-the-handheld score sheet, and a "today's pick" with a reason ("unplayed for 3 months, 20 minutes long").
14. **Photo diary (S/M).** Optional auto-screenshot every N minutes in a game, kept in a per-game album, with a flip-through view in the gallery and a stitched strip for a playthrough. Screenshots and the gallery tool exist.
15. **Sleep timer for games (S).** Like a music player's: "stop after 30 minutes", with a warning, an auto-save and a power off. Daily play budgets (a gentle limit with a PIN) fit the same code.
16. **RAM cheat finder (L).** Search the core's memory for a value that changed (the Cheat Engine way), then save the address as a cheat that the cheat page already understands. RetroArch has it behind a desktop-sized menu; a handheld friendly version is open ground.

**Make the network useful**
17. **Phone as keyboard and drop box (M).** The unit shows a QR code; a web page on the phone types into the search box, uploads ROMs and art, and downloads saves and screenshots (needs a small HTTP server in a tool; the Moss image already has the pieces).
18. **Homebrew browser (M).** The PicoSearch idea for free homebrew: browse and download freely distributed GB, GBA, NES and Genesis homebrew from public sources with screenshots and descriptions, straight into the right ROM folder.
19. **Cart update checker (S).** PicoSearch remembers which BBS thread each cart came from; "check for newer versions" compares and offers the new file (keeping the old).
20. **Save sharing (M).** Export a save state with its picture as one small file or a link, and import it on another unit (speedrun strats, "start from the boss").

**Nicer look (square screen)**
21. **Shelf view (M).** The library as a grid of the ASCII cartridge tiles instead of a list, scrolling by rows; the 720 x 720 panel is made for it.
22. **Themes that move with the day (S).** Per-system accent colours and an automatic dim, warm night theme at a set hour (the theme tool and the clock exist).

**If I had to pick the first three:** learned CPU speed per game (1), crash-proof sessions (4), and backlog and notes (7): the first uses today's CPU fix for something nobody else can do, the second removes a real fear, the third is small and shows on every card.

## Housekeeping that protects the work
- **CI.** Run `tools/host-tests/run.sh` and the Docker build on every push (GitHub Actions or a local hook). The tests exist; nothing runs them automatically.
- **Releases.** Done: `tools/package/release.sh <package> --publish` makes the GitHub release and the web installer's copy. Remember `tools/package/install.sh` (not the copy in `test-package`) is what ships.
- **Pin and mirror dependencies.** Every dependency is pinned to a commit; mirror them so a deleted upstream cannot break the build.
- **Move `moss/` forward.** Build Moss once with the patches so they stop being untested (the firmware builds in `rgb20pro-firmware` are the route).
- **Share the duplicated code.** The Wi-Fi checks, `shell_quote`, `copy_file` and the curl job exist twice (Romulus and PicoSearch), `loadFit` twice (gallery, Switcher), the recents list twice (launcher, `utils.c`). One copy in `common/` with one test each.

## What I would do next
1. **Try the new things on the device** and report what is off: the Switcher (Fn), auto-save on quit, delete game, the PicoSearch browse menu.
2. **Learned CPU speed per game (new idea 1)**, **crash-proof sessions (4)** and **backlog and notes (7)** from the list above.
3. **Save backup (M11)** and **ROM drop over Wi-Fi (M12)**: the two ways the card is still a single point of failure and a chore.
4. Keep the **silent-audio** diagnostics in place and **test Wi-Fi away from home**.

## What I need from you to go further
- A note on which of the Switcher, auto-save on quit, delete game and the PicoSearch browse menu misbehave on the device, with what you saw.
- Whether a Moss image build (hours on GitHub Actions) is worth doing for the kernel patch, a trimmed Moss and a custom boot logo.
- The next time audio goes silent: power off without launching anything else, card to the Mac (logs are already there).
