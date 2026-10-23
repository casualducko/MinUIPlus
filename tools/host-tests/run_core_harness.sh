#!/bin/sh
# Run a real libretro core through the real audio code (workspace/all/common/api.c) on aarch64,
# with the device's compiler and flags, and print what the audio thread got.
#
#   run_core_harness.sh CORE.so GAME [seconds] [pace_fps] [one-frame-calls 0|1]
#   NO_DRC=1 run_core_harness.sh ...      the same without the audio rate control
#
# Needs Docker, the rgb30-toolchain image (make PLATFORM=rgb30 build creates it) and
# the core and game, eg. the fceumm core and a NES ROM from a MinUI card. Pace the
# frontend at 60.0 to reproduce the device: it runs frames at about 60 fps while NES
# makes audio for 60.0988.
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$here/../..
core=${1:?core .so}; game=${2:?game file}; seconds=${3:-15}; pace=${4:-60.0}; single=${5:-0}
work=${WORK:-${TMPDIR:-/tmp}/core-harness}
mkdir -p "$work/lib"

# the toolchain image's C library, so the device's own core runs unmodified
if [ ! -f "$work/lib/libc.so.6" ]; then
	c=$(docker create rgb30-toolchain)
	sys=/work/MOSS/build.MOSS-RK3566.aarch64/toolchain/aarch64-libreelec-linux-gnueabi/sysroot/usr/lib
	for f in ld-linux-aarch64.so.1 libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0; do docker cp -L "$c:$sys/$f" "$work/lib/$f"; done
	docker rm "$c" >/dev/null
fi

python3 "$here/extract.py" "$repo/workspace/all/common/api.c" '#define MAX_SAMPLE_RATE' 'void SND_init' > "$work/snd.inc"
cp "$here/core_harness.c" "$work/harness.c"
cp "$repo/workspace/all/minarch/libretro-common/include/libretro.h" "$work/" # present after a build
cp "$core" "$work/core.so"; cp "$game" "$work/game.rom"

docker run --rm -v "$work":/rig rgb30-toolchain /bin/bash -c '. ~/.bashrc && cd /rig && ${CROSS_COMPILE}gcc -mtune=cortex-a55 -march=armv8.2-a -O3 -fomit-frame-pointer -Ofast -std=gnu99 -Os -flto -w -DSAMPLES=400 -DSND_INC=\"snd.inc\" -I. -o harness harness.c -ldl -lpthread -lm'
docker run --rm --platform linux/arm64 ${NO_DRC:+-e NO_DRC=1} -v "$work":/rig alpine:3 \
	/rig/lib/ld-linux-aarch64.so.1 --library-path /rig/lib /rig/harness /rig/core.so /rig/game.rom "$seconds" "$pace" "$single"
