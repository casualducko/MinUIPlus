MinUIPlus: an update package for a Powkiddy RGB20SX (Moss/JELOS) card that already runs MinUI
=============================================================================================

The tag of this build is in the file VERSION. Everything this package changes is described in
FORK.md in the source repository (https://github.com/casualducko/MinUIPlus).

Install or update
-----------------
Easiest: on the device, Tools > Update (needs Wi-Fi), or the web installer at
https://casualducko.github.io/MinUIPlus/ (a Chromium browser, the card in the computer).

By hand, with the games card (the one with the .system folder) in a Mac or Linux computer:

    ./install.sh /Volumes/RGB20SX

It replaces the files this package ships that the card already has, and it adds the new tools
(Switcher, PicoSearch, Stats, Themes, ASCIIArt, Info, Update, Screenshots, Settings, SSH, Romulus,
the fallback font). Before it replaces anything it copies the old file to
.system/rgb30/backup-before-fork-<time>/ on the card. If a copy fails (a full or read-only card) it
says so and stops without marking the card as updated.

Undo:

    ./revert.sh /Volumes/RGB20SX

It puts back every file from the oldest backup that has it. Files that install.sh only added are
left where they are; delete them by hand.

Your saves, ROMs, save states, settings and collections are never touched. The system card (TF1,
the one Moss itself is on) is not involved at all.

What is in the package
----------------------
.system/rgb30/bin/        minui.elf minarch.elf keymon.elf say.elf   (the launcher, the emulator frontend, the key daemon)
.system/rgb30/lib/        libmsettings.so
.system/rgb30/paks/       MinUI.pak/launch.sh (the boot script)
.system/res/              the Japanese fallback font and its license
Tools/rgb30/              the tools: Info, Switcher, Screenshots, Stats, Themes, ASCIIArt, Clock, Wi-Fi, SSH,
                          Romulus, PicoSearch, Settings, Update, Input
Emus/rgb30/MGBA.pak/      the mGBA core (the original one: newer ones crash GBA games on this unit)
examples/                 a sample theme.cfg
SHA256SUMS                the checksum of every file above (install.sh checks nothing itself; the Update tool
                          and the web installer check the whole package and these)

Things to know
--------------
- A new launcher or tool is used from the next time the launcher starts (start a game or a tool and
  come back).
- The CPU speed scaling on this unit needs a change to the Moss system card (a device tree), which
  this package cannot make. See MOSS-CPU.md and the moss/ folder in the source repository.
- Settings are in Tools > Settings; the files behind them are in .userdata/rgb30/ (for example
  show-new, show-art, autosave-quit, titleart-style, model.txt).
- If something is wrong, the log of the last game or tool of each system is
  .userdata/rgb30/logs/<system>.txt on the card.
