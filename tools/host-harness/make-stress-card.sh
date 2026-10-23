#!/bin/sh
# Add nasty content to a card made by make-card.sh: 3000 ROMs, a malformed map.txt, an m3u
# pointing at missing discs, garbage collections and a corrupt recent.txt (a 3000 character
# line, no tab, missing files, CRLF). usage: make-stress-card.sh CARD_DIR
# NOTE: run.sh deletes recent.txt before each run; run the built $WORK/minui directly to
# keep it (see README).
set -e
CARD=${1:?card dir}
python3 -I - "$CARD" <<'PY'
import os, sys, random
card = sys.argv[1]
fc = os.path.join(card, "Roms", "NES (FC)")
random.seed(7)
for i in range(3000):
    open(os.path.join(fc, "Game %04d (USA).nes" % i), 'w').close()
lines = ["Game %04d (USA).nes\tAliased %d" % (i, i) for i in range(0, 3000, 7)]
lines += ["no tab here", "", "\t", "\talias with no key", "key with no alias\t", "x"*5000 + "\tlong key",
          "Game 0001 (USA).nes\t" + "y"*5000, "missing.nes\tGhost", "Game 0002 (USA).nes\tdup1",
          "Game 0002 (USA).nes\tdup2", "\r", "Game 0003 (USA).nes\tcrlf alias\r"]
open(os.path.join(fc, "map.txt"), 'w', newline='').write("\n".join(lines) + "\n")
open(os.path.join(fc, "Multi.m3u"), 'w').write("missing disc 1.cue\nmissing disc 2.cue\n\n" + "z"*600 + "\nMulti.m3u\n")
col = os.path.join(card, "Collections"); os.makedirs(col, exist_ok=True)
open(os.path.join(col, "Garbage.txt"), 'wb').write(os.urandom(4000) + b"\n/Roms/NES (FC)/Game 0001 (USA).nes\n" + b"/" + b"a"*2000 + b"\n/../../etc/passwd\n")
open(os.path.join(col, "Favorites.txt"), 'w').write("/Roms/NES (FC)/Game 0005 (USA).nes\n/Roms/NES (FC)/Game 0006 (USA).nes\r\n/Roms/NES (FC)/nothere.nes\n")
open(os.path.join(col, "Empty.txt"), 'w').write("")
open(os.path.join(card, ".userdata/shared/.minui/recent.txt"), 'w', newline='').write(
    "/Roms/NES (FC)/Game 0010 (USA).nes\tTen\r\n" + "/Roms/" + "q"*3000 + "\tlong\n" + "nonsense without a tab\n" +
    "/Roms/NES (FC)/gone.nes\tGone\n" + "\n" + "/Roms/NES (FC)/Game 0011 (USA).nes\n" + "\t\n")
PY
echo "stress content added to $CARD"
