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
x "$all/common/api.c" 'static void GFX_utf8Trim' 'int GFX_truncateText' > "$out/gen/GFX_utf8Trim.inc"
x "$all/common/api.c" 'int GFX_truncateText' 'int GFX_wrapText' > "$out/gen/GFX_truncate.inc"
x "$all/common/utils.c" 'void getFile' 'char* allocFile' > "$out/gen/getFile.inc"
x "$all/common/utils.c" '// Saves must never be destroyed' '// Keeping older versions' > "$out/gen/writeFileAtomic.inc"
x "$all/common/utils.c" '// Keeping older versions' 'int mkdirs' > "$out/gen/backup.inc"
x "$all/common/utils.c" 'void putFile' 'void getFile' > "$out/gen/putFile.inc"
x "$all/common/utils.c" 'int mkdirs' 'void normalizeNewline' > "$out/gen/mkdirs.inc"
x "$all/minarch/minarch.c" '// Older versions of the save are kept' 'static void RTC_getPath' > "$out/gen/SRAM_versions.inc"
x "$all/minui/minui.c" 'static void splitVersion' 'static int getFirstDisc' > "$out/gen/splitVersion.inc"
x "$all/minui/minui.c" 'static int getFirstDisc' 'static void addEntries' > "$out/gen/getFirstDisc.inc"
x "$all/common/api.c" '#define SLEEP_TIMEOUT_DEFAULT' 'void PWR_update' > "$out/gen/PWR_sleep.inc"
x "$all/common/api.c" 'static int SND_parseBufferMs' 'void SND_init' > "$out/gen/SND_bufferMs.inc"
x "$all/common/api.c" '// How strong the motor runs' 'static struct VIB_Context' > "$out/gen/VIB_scale.inc"
x "$all/common/api.h" '// theme colors' 'extern uint32_t RGB_WHITE;' > "$out/gen/theme_types.inc"
x "$all/common/api.c" '// The theme file,' '// end of the theme file code' > "$out/gen/theme.inc"
x "$all/common/utils.h" '// play time, kept' 'int Playtime_parse' > "$out/gen/playtime_types.inc"
x "$all/common/utils.c" '// Play time (see utils.h)' '// end of the play time code' > "$out/gen/playtime.inc"
x "$all/minarch/minarch.c" '// Cheats: RetroArch-style' '// end of the cheat file code' > "$out/gen/cheats.inc"
x "$all/minarch/minarch.c" '// Turbo (see turbo_mask)' '// end of the turbo code' > "$out/gen/turbo.inc"
x "$all/common/utils.c" '// Cheat database (see utils.h)' '// end of the cheat database code' > "$out/gen/cheatdb.inc"
x "$all/romulus/romulus.c" '// a server (or a Wi-Fi login page' 'static void finish_download' > "$out/gen/romulus_html.inc"
x "$all/common/utils.c" '// UTF-8 (see utils.h)' '// end of the UTF-8 code' > "$out/gen/utf8.inc"
x "$all/common/utils.c" '// Random game (see utils.h)' '// end of the random game code' > "$out/gen/random.inc"
x "$all/common/utils.c" '// HTTP date (see utils.h)' '// end of the HTTP date code' > "$out/gen/httpdate.inc"
x "$all/common/utils.c" '// Recently played (see utils.h)' 'void Recents_add(const char* sd_path) {' > "$out/gen/recents.inc"
x "$all/common/api.c" 'static int PAD_parseDeadzone' 'static int PAD_deadzone' > "$out/gen/PAD_deadzone.inc"
x "$all/../rgb30/libmsettings/msettings.c" '#define SETTINGS_VERSION 2' '#define SHM_KEY' > "$out/gen/msettings_types.inc"
x "$all/minarch/minarch.c" 'void Core_getName' '// Cheats: RetroArch-style' > "$out/gen/Core_getName.inc"
x "$all/common/api.c" 'int GFX_wrapText' '///////////////////////////////

// scale_blend' > "$out/gen/GFX_wrapText.inc"
x "$all/common/api.c" 'void GFX_blitMessage' '// The slot picker under the save and load previews' > "$out/gen/GFX_blitMessage.inc"
x "$all/minarch/minarch.c" '	int claimed = 0;' '	static int toggled_ff_on' > "$out/gen/inputclaim_claim.inc"
x "$all/minarch/minarch.c" '	buttons = 0;
	for (int i=0; config.controls[i].name; i++) {' '	// if (buttons) LOG_info' > "$out/gen/inputclaim_controls.inc"

failed=0
for t in rumble sram lowbattery backup bufferms deadzone theme cheats cheatdb titleart recents httpdate random info gallery utf8 turbo romulushtml playtime emuname corename msettings underrun fadein sleeptimeout firstdisc mkdirs version getfile atomicwrite audio_sim truncate wraptext blitmessage inputclaim; do
	echo "== $t"
	$CC $FLAGS -I"$out" -I"$all/titleart" -I"$all/clock" -I"$all/info" -I"$all/gallery" -I"$all/common" -o "$out/test_$t" "$here/test_$t.c" -lm
	if ! "$out/test_$t" "$here"; then failed=1; echo "   ^ FAILED: $t"; fi
done
# the Clock tool's network part against a mock time server
echo "== clocknet"
$CC $FLAGS -I"$out" -I"$all/clock" -I"$all/common" -o "$out/test_clocknet" "$here/test_clocknet.c" -lm
rm -f "$out/mock-time.log"
python3 "$here/mock_time.py" > "$out/mock-time.log" 2>&1 &
mocktime=$!
tport=
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
	tport=$(sed -n 's/^PORT //p' "$out/mock-time.log" 2>/dev/null | head -1)
	[ -n "$tport" ] && break
	sleep 0.25
done
if ! (cd "$out" && CLOCK_URL="http://127.0.0.1:$tport/" ./test_clocknet); then failed=1; echo "   ^ FAILED: clocknet"; fi
kill $mocktime 2>/dev/null
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

# the Wi-Fi tool: parsing iw output, the saved networks file, running commands
echo "== wifi"
$CC $FLAGS -I"$all/wifi" -o "$out/test_wifi" "$here/test_wifi.c" "$all/wifi/wifi_core.c"
if ! "$out/test_wifi"; then failed=1; echo "   ^ FAILED: wifi"; fi

echo
if [ $failed = 0 ]; then echo "all host tests passed"; else echo "SOME HOST TESTS FAILED"; exit 1; fi
