#!/bin/sh
# Host-side regression tests for bugs fixed in this fork. Each test compiles the
# real functions out of workspace/all (see extract.py), so it always tests the
# current source. Needs a C compiler and python3, nothing else (no SDL).
#
#   tools/host-tests/run.sh          run everything
#   CC=gcc tools/host-tests/run.sh   pick a compiler
#   SRC=/other/workspace/all tools/host-tests/run.sh   test another checkout

set -e
here=$(cd "$(dirname "$0")" && pwd)
all=${SRC:-"$here/../../workspace/all"} # SRC=path/to/workspace/all to test another checkout
out=${TMPDIR:-/tmp}/minui-host-tests
mkdir -p "$out/gen"
CC=${CC:-cc}
FLAGS="-g -Wall -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined"

x() { python3 "$here/extract.py" "$@"; } # FILE START END

x "$all/common/utils.c" 'void getEmuName' 'void getEmuPath' > "$out/gen/getEmuName.inc"
x "$all/common/api.c" 'static void SND_audioCallback' 'static void SND_resizeBuffer' > "$out/gen/SND_audioCallback.inc"
x "$all/common/api.c" 'int GFX_truncateText' 'int GFX_wrapText' > "$out/gen/GFX_truncate.inc"
x "$all/common/api.c" 'int GFX_wrapText' '///////////////////////////////

// scale_blend' > "$out/gen/GFX_wrapText.inc"
x "$all/common/api.c" 'void GFX_blitMessage' 'int GFX_blitHardwareGroup' > "$out/gen/GFX_blitMessage.inc"
x "$all/minarch/minarch.c" '	int claimed = 0;' '	static int toggled_ff_on' > "$out/gen/inputclaim_claim.inc"
x "$all/minarch/minarch.c" '	buttons = 0;
	for (int i=0; config.controls[i].name; i++) {' '	// if (buttons) LOG_info' > "$out/gen/inputclaim_controls.inc"

failed=0
for t in emuname underrun truncate wraptext blitmessage inputclaim; do
	echo "== $t"
	$CC $FLAGS -I"$out" -o "$out/test_$t" "$here/test_$t.c"
	if ! "$out/test_$t" "$here"; then failed=1; echo "   ^ FAILED: $t"; fi
done
echo
if [ $failed = 0 ]; then echo "all host tests passed"; else echo "SOME HOST TESTS FAILED"; exit 1; fi
