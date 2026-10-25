MinUI fork, test build (commit 5a3a94f)
=======================================

Built from MinUI/ in this folder's parent, branch "fixes". To tell this build from
the stock one: in a game, Menu > Options shows a "Frontend" line reading
"MinUI (2026.10.25 3e0313f)" (stock says "2025.11.27 dbf8943"). The launcher's own
version screen shows the card's version.txt, which this doesn't change.

These fixes were NOT tested on the device (nobody could while this was made).
The host tests and the launcher-on-a-Mac checks pass, but please try them.

Install (the TF2 card, the one with the .system folder; put it in the Mac)
-------
    ./install.sh /Volumes/RGB20SX

It only replaces files the card already has, and first copies each one to
.system/rgb30/backup-before-fork-<time>/ on the card. Undo with:

    ./revert.sh /Volumes/RGB20SX

(Your saves, ROMs and settings are never touched. TF1 isn't involved at all.)

What to try
-----------
1. Volume keys: + should raise the volume, - lower it. MENU + volume: brightness.
   If they turn out to be the wrong way round on your unit after all, create an
   empty file named swap-volume-keys in .userdata/rgb30/ on the card.
2. In a game: Menu > Options > Shortcuts, bind something to MENU+X, then use it.
   The game should no longer also receive an X press. Binding the same MENU+button
   twice (a control and a shortcut) now shows a message and refuses the second.
3. A system folder with games but no emu pak for one of its parts (SFC next to
   SUPA without SUPA.pak) no longer lists the games of the part with no pak.
4. Long titles that share a name keep their (USA)/(Europe)-style tag instead of
   showing identical truncated rows.
5. Audio: the rate control (which keeps the NES/SNES buffer from draining) now aims for a
   slightly fuller buffer, which should mean fewer of the remaining small crackles. Each
   game start also logs the sound chip's mixer state. If the device is ever silent again,
   leave the card as it is and send me .userdata/rgb30/logs/<system>.txt: the "audio:" lines
   show whether sound was produced and handed on. To compare with the rate control off, put
   an empty file named no-audio-rate-control in .userdata/rgb30/ on the card.
   (The pop at game load is not addressed yet.)

What's in here
--------------
.system/rgb30/bin/        minui.elf minarch.elf keymon.elf say.elf (the rebuilt ones)
.system/rgb30/lib/        libmsettings.so (a corrupt msettings.bin can no longer set a silly volume)
Tools/rgb30/Clock.pak/    clock.elf   (24 hour mode cursor fix)
Tools/rgb30/Input.pak/    minput.elf
Tools/rgb30/Wi-Fi.pak/    launch.sh   (a wifi.txt saved on Windows now works)
No cores are included: the ones on your card were already identical to what the
pinned build produces.

Not tried at all: the Moss patches in ../moss-patches/ (they need a Moss build and a
card flash, so they're for later).

New in 79e6d5d (also not heard on the device yet)
-------------------------------------------------
- In a game, change the volume (or MENU + volume for brightness): the same pill as in
  the launcher should show at the top right for about a second.
- The first 50 ms of each game's sound fades in, to take the pop out of game starts.
  Tell me if the pop is gone, less, or the same (the same means it is the sound chip).

- It adds diagnostics only: at game start and quit the log (.userdata/rgb30/logs/<system>.txt)
records the codec's Playback Path, its mixer, the ALSA stream state and PipeWire's default sink.

- the Fn button (evdev 316) now acts as MENU like L3/R3 (Fn+volume = brightness).

- It adds the Romulus tool (Tools > Romulus). Before using it, edit .userdata/rgb30/romulus.cfg
on the card (the installer makes it empty): url=https://your-server and token=your-token.
Then: Wi-Fi on, open Romulus, pick a platform, A downloads, X searches, B goes back.

- autosleep after 2 minutes of no input (was 30 s). To change it, put a number of seconds in .userdata/rgb30/sleep-timeout (0 = never).

- overnight review fixes (multi-disc launching regression, version screen crash, shell mkdir in minarch, Romulus network check by any interface). See FORK.md.

- saves (states, SRAM, RTC, config, recents) are replaced atomically, so a failed or interrupted save keeps the old one.

- Tools > Sleep & Power sets how long until sleep and until power-off (or never for either). It writes
  .userdata/rgb30/sleep-timeout and poweroff-timeout (seconds, 0 = never); applies the next time a game or the launcher starts.
- In a game, MENU+SELECT saves a screenshot to Screenshots/<system>/ on the card. The left stick threshold is in
  .userdata/rgb30/stick-deadzone (percent, 10-90, default 40).
- putFile (backlight, CPU governor writes) works again: the atomic-write change had broken sleep's screen-off.
- .system/rgb30/paks/MinUI.pak/launch.sh applies the Moss timezone at boot (the clock showed UTC before).
- Tools > Power & Audio also sets an audio buffer (.userdata/rgb30/audio-buffer, milliseconds): bigger rides out busy moments in a game, adds that much delay.
- Save versions: each save keeps up to 3 earlier versions in Saves/<system>/.backups/, restored from Options > Restore Save in a game. Low battery shutdown at 5% (Tools > Power & Audio) quicksaves and powers off.
- Tools > Wi-Fi is the new Wi-Fi tool (scan, choose a network, type a password, several saved networks). The earlier one stays as Wi-Fi.pak/launch-old.sh.
- Rumble works (if the motor is reachable): Tools > Settings has a Rumble strength row that buzzes when you change it and says whether a motor was found.

New in 79e6d5d (theme, cheats, play stats; none tried on the device yet)
------------------------------------------------------------------------
- .userdata/rgb30/theme.cfg: colors and font (see FORK.md "Theme file").
- Menu > Options > Cheats, from Cheats/<system>/<game>.cht (see FORK.md "Cheats").
- Tools > Stats: play time per game (counted from this version on).
- Tools > Settings: the Rumble row says the RGB20SX has no rumble motor.
- examples/theme.cfg: a warm colour theme to copy to .userdata/rgb30/theme.cfg.

New in 3e0313f (not tried on the device yet)
--------------------------------------------
- Menu > Options > Cheats: when a game has no cheat file, A downloads it from the libretro cheat
  database (the files RetroArch uses) over Wi-Fi. Exact name first, then the closest name in the
  system's folder. B cancels. Only games whose title is in the database will find anything.

New in 1812385 (not tried on the device yet)
--------------------------------------------
- Romulus: after a download, the "Downloaded" screen has A = PLAY (starts the game) and B = BACK.
- Options pages (Cheats etc.): when a name is cut off, the bottom line says "Press Y to read the whole line"; Y shows it all.

New in d196427: Tools > Theme (not tried on the device yet)
----------------------------------------------------------------------
- Pick a preset or edit each color (red/green/blue sliders or a palette) with a live preview, then
  "Save and apply". It writes .userdata/rgb30/theme.cfg; theme.cfg now also takes background=RRGGBB.
  "Reset to stock" and Save, or just delete theme.cfg, goes back to the stock look.

New in 55e8986: Tools > TitleArt, and a Game art on/off switch (not tried on the device yet)
----------------------------------------------------------------------
- Tools > TitleArt: A makes a title-in-ASCII picture for every game without art (the launcher shows it
  at the right). X remakes the tool's own tiles (after a theme change). Art that is already there is never replaced.
- Y in TitleArt, or Tools > Settings > Game art: On/Off. Off = text only in the launcher.

New in 4c7e588 (not tried on the device yet)
----------------------------------------------------------------------
- Romulus makes the title picture for a game right after it is downloaded (when the game art is on).
- Tools > Settings > "Art for new games": Automatic makes pictures for new games at every boot (a few seconds at most; takes effect after a reboot).
- Long titles are never cut off in the pictures (condensed letters, or plain text for a very long one).
- The selected name in the launcher scrolls when it does not fit; duplicate-title tags are shorter (no file extension).

New in 62184ca: the Power tool is now called Settings
----------------------------------------------------------------------
- Tools > Settings (it used to be listed as "Power"). The rows are in alphabetical order.
- install.sh moves an old Tools/rgb30/Power.pak into its backup folder.

Overnight 2026-10-25 (build 5a3a94f): six features and a code review. NONE of it is tried on the device yet.
-----------------------------------------------------------------------------------------------------------------------------
New:
- Tools > Clock: Y sets the clock from the network (Wi-Fi comes up by itself; B cancels).
- Launcher: Y picks a random game (a never-played one if there is one; at the root, in a random folder).
- Tools > Info: battery, Wi-Fi, IP, card, memory, play time, CPU, uptime, build.
- Tools > Screenshots: browse, step through, delete the pictures MENU+SELECT took.
- Japanese titles show everywhere (a fallback font, .system/res/fallback.ttf, Noto Sans JP, license next to it).
- Options > Frontend > Turbo A / B / X / Y / L / R and Turbo Speed (saved per game with Save Changes).
- Tools > Settings > "Art for new games" (Automatic makes title pictures at boot, a few seconds at most) and Romulus makes a picture for each game it downloads.
Fixed (see FORK.md "Code review"): a long cheat code could overflow the stack; a game froze if audio failed to open;
  saving the config could delete a game's own config; Romulus could replace a ROM already on the card and saved web pages as games;
  a wrong Wi-Fi password left the bad key stored; the SSH tool now turns passwords off when a key is installed;
  revert.sh aborted on a card that had the old Power tool; and more.
Install: ./install.sh /Volumes/RGB20SX (backs everything up; ./revert.sh undoes it; new tools and the font are added, the old
  Power tool is moved into the backup). Over SSH the same files go the way the earlier builds did.
To try first: 1. Tools > Clock, Y. 2. In the launcher, Y. 3. Tools > Info. 4. Play something, MENU+SELECT, then Tools > Screenshots.
  5. A game with a Japanese name. 6. Options > Frontend > Turbo A in a game with a fire button. 7. Tools > TitleArt, X (remakes the tiles).
Note for SSH: with a key installed, password logins are off; if that ever locks you out, delete /storage/.cache/services/sshd.conf's SSH_ARGS line from a card.

UPDATE TOOL
Tools > Update (A checks, A installs) takes new builds from the project's GitHub releases. Settings live in
.userdata/rgb30/update.cfg (repo, and a token if the repo is private). Everything it replaces is backed up
exactly as install.sh does. To make a release from this folder: MinUI/tools/package/release.sh <this folder> --publish
