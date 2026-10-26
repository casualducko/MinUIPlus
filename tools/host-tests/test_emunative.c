// Emu_isNative() from utils.c: an emulator pak whose launch.sh does not run minarch records no play time itself
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int exists(char* path) { struct stat st; return stat(path, &st)==0; }
#include "gen/allocFile.inc"
#include "gen/emunative.inc"

static int fails = 0;
static void put_text(const char* path, const char* text) { FILE* f = fopen(path, "w"); fputs(text, f); fclose(f); }
#define CHECK(c, msg) do { if (c) printf("ok    %s\n", msg); else { printf("FAIL  %s\n", msg); fails++; } } while (0)
int main(void) {
	const char* a = "/tmp/emunative-minarch.sh";
	const char* b = "/tmp/emunative-native.sh";
	put_text(a, "#!/bin/sh\nEMU_EXE=fake08\n\"$SYSTEM_PATH/bin/minarch.elf\" \"$CORES_PATH/${EMU_EXE}_libretro.so\" \"$ROM\"\n");
	put_text(b, "#!/bin/sh\ncd \"$PICO8_DIR\"\n./pico8_64 -run \"$1\" -pixel_perfect 1 -joystick 0\n");
	CHECK(!Emu_isNative(a), "a pak that runs minarch is not native");
	CHECK(Emu_isNative(b), "the native PICO-8 pak is");
	CHECK(!Emu_isNative("/tmp/emunative-no-such-file.sh"), "no pak: not native (nothing to note)");
	unlink(a); unlink(b);
	printf("%s\n", fails ? "SOME FAILED" : "all passed");
	return fails != 0;
}
