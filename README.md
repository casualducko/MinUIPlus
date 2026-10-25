# MinUIPlus

A maintained fork of [MinUI](https://github.com/shauninman/MinUI) for the **Powkiddy RGB20SX** running Moss/JELOS (720x720 screen). It keeps MinUI's simple launcher and adds the things that were missing on this handheld: working volume and Fn keys, sleep and power settings, cheats, a settings app, Wi-Fi, a self-updater and more.

> **Status:** built for and tested on one RGB20SX only. It might work on the Powkiddy RGB30 (same screen and chip). Other devices need their own platform work. Use at your own risk, and keep your backups.

## Install or update

This is an add-on for a card that already runs MinUI under Moss; it does not install Moss.

- **From the browser (Chrome, Edge or another Chromium browser):** https://casualducko.github.io/MinUIPlus/ , pick the games card and press Install. It backs up everything it replaces.
- **On the device:** Tools > Update checks the latest release over Wi-Fi.
- **By hand:** download the package from the [latest release](https://github.com/casualducko/MinUIPlus/releases/latest), unpack it, run `./install.sh /path/to/card` (a backup of replaced files goes on the card; `revert.sh` undoes it).

## What it adds

- **Launcher:** Y picks a random game from the whole library, SELECT opens Tools, the Fn page shows system info (battery, Wi-Fi, IP, card, memory, versions) and turns Wi-Fi on and off with X, an optional clock beside the Wi-Fi and battery icons, scrolling long names, Japanese titles.
- **In games:** fixed volume keys and audio, numbered save slots, screenshots, cheats (download per game, Turn All Cheats Off), turbo buttons, restore earlier saves.
- **Tools:** Settings, Info, Screenshots gallery, Stats, Theme, Title Art, Clock (sets itself from the network), Wi-Fi, SSH, Romulus (download games from your own server), Update.

Details of every change are in [FORK.md](FORK.md); ideas and open items are in [IDEAS.md](IDEAS.md).

## Build and test

The device build runs in Docker (`make PLATFORM=rgb30 build`). The host tests (`tools/host-tests/run.sh`) and the Mac harnesses (`tools/host-harness/`) run without a device. Releases are made with `tools/package/release.sh`.

## License and credits

MinUI is by Shaun Inman and has no license file; this fork's own additions are MIT-licensed. [NOTICE.md](NOTICE.md) says exactly what is covered and lists the third-party parts. The original MinUI README is `UPSTREAM-README.md`.
