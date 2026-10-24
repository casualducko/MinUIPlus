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
x "$all/common/api.c" '#define MAX_SAMPLE_RATE' 'void SND_init' > "$out/gen/SND_core.inc"
x "$all/common/api.c" 'static void SND_audioFill' 'static void SND_resizeBuffer' > "$out/gen/SND_audioCallback.inc"
x "$all/common/api.c" 'int GFX_truncateText' 'int GFX_wrapText' > "$out/gen/GFX_truncate.inc"
x "$all/common/utils.c" 'void getFile' 'char* allocFile' > "$out/gen/getFile.inc"
x "$all/common/utils.c" 'int mkdirs' 'void normalizeNewline' > "$out/gen/mkdirs.inc"
x "$all/minui/minui.c" 'static void splitVersion' 'static int getFirstDisc' > "$out/gen/splitVersion.inc"
x "$all/minui/minui.c" 'static int getFirstDisc' 'static void addEntries' > "$out/gen/getFirstDisc.inc"
x "$all/common/api.c" '#define SLEEP_TIMEOUT_DEFAULT' 'void PWR_update' > "$out/gen/PWR_sleep.inc"
x "$all/../rgb30/libmsettings/msettings.c" '#define SETTINGS_VERSION 2' '#define SHM_KEY' > "$out/gen/msettings_types.inc"
x "$all/minarch/minarch.c" 'void Core_getName' 'void Core_open' > "$out/gen/Core_getName.inc"
x "$all/common/api.c" 'int GFX_wrapText' '///////////////////////////////

// scale_blend' > "$out/gen/GFX_wrapText.inc"
x "$all/common/api.c" 'void GFX_blitMessage' 'int GFX_blitHardwareGroup' > "$out/gen/GFX_blitMessage.inc"
x "$all/minarch/minarch.c" '	int claimed = 0;' '	static int toggled_ff_on' > "$out/gen/inputclaim_claim.inc"
x "$all/minarch/minarch.c" '	buttons = 0;
	for (int i=0; config.controls[i].name; i++) {' '	// if (buttons) LOG_info' > "$out/gen/inputclaim_controls.inc"

failed=0
for t in emuname corename msettings underrun fadein sleeptimeout firstdisc mkdirs version getfile audio_sim truncate wraptext blitmessage inputclaim; do
	echo "== $t"
	$CC $FLAGS -I"$out" -o "$out/test_$t" "$here/test_$t.c" -lm
	if ! "$out/test_$t" "$here"; then failed=1; echo "   ^ FAILED: $t"; fi
done
# the Romulus tool: JSON, zip, config, naming, and the curl job against a mock server
echo "== romulus"
python3 "$here/make_zips.py" "$out/zips" > /dev/null
tok="test-token-$$"
rm -f "$out/mock.log"
python3 "$here/mock_romulus.py" "$tok" > "$out/mock.log" 2>&1 &
mock=$!
port=
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
	port=$(sed -n 's/^PORT //p' "$out/mock.log" 2>/dev/null | head -1)
	[ -n "$port" ] && break
	sleep 0.25
done
$CC $FLAGS -I"$all/romulus" -o "$out/test_romulus" "$here/test_romulus.c" "$all/romulus/json.c" "$all/romulus/zipx.c" "$all/romulus/rapi.c" -lz
if ! ROM_ZIPS="$out/zips" ROM_PORT="$port" ROM_TOKEN="$tok" "$out/test_romulus"; then failed=1; echo "   ^ FAILED: romulus"; fi
kill $mock 2>/dev/null; wait $mock 2>/dev/null || true

echo
if [ $failed = 0 ]; then echo "all host tests passed"; else echo "SOME HOST TESTS FAILED"; exit 1; fi
