#!/bin/sh
# Runs a real libretro core on aarch64 (like run_core_harness.sh), turns a cheat on through
# retro_cheat_set the way minarch does, and prints the game's RAM before, with the cheat, and after.
#   run_cheat_harness.sh CORE.so GAME ADDR_HEX CODE      eg. fceumm_libretro.so game.nes 0011 0011:5A
# Needs the rgb30-toolchain image and a core-harness lib folder (run_core_harness.sh makes it).
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$here/../..
core=${1:?core .so}; game=${2:?game}; addr=${3:?ram address, hex}; code=${4:?cheat code}
work=${WORK:-${TMPDIR:-/tmp}/cheat-harness}; libs=${LIBS:-${TMPDIR:-/tmp}/core-harness/lib}
mkdir -p "$work"
cp "$here/cheat_harness.c" "$work/"; cp "$repo/workspace/all/minarch/libretro-common/include/libretro.h" "$work/"
cp "$core" "$work/core.so"; cp "$game" "$work/game.rom"
docker run --rm -v "$work":/rig rgb30-toolchain /bin/bash -c '. ~/.bashrc && cd /rig && ${CROSS_COMPILE}gcc -O1 -std=gnu99 -w -I. -o cheat_harness cheat_harness.c -ldl'
docker run --rm --platform linux/arm64 -v "$work":/rig -v "$libs":/rig/lib alpine:3 /rig/lib/ld-linux-aarch64.so.1 --library-path /rig/lib /rig/cheat_harness /rig/core.so /rig/game.rom "$addr" "$code"
