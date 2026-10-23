// Core_getName() from minarch.c: "gambatte_libretro.so" -> "gambatte". It used to crash on a
// core file with no underscore in its name and overflow core.name (128 bytes) on a long one.
#include <stdio.h>
#include <string.h>
#include <libgen.h>
static struct { const char name[128]; char guard[16]; } core;
#include "gen/Core_getName.inc"

static int fails = 0;
static void check(const char* in, const char* want) {
	char path[512]; snprintf(path, sizeof(path), "%s", in);
	memset(core.guard, 'G', sizeof(core.guard));
	Core_getName(path, (char*)core.name);
	int ok = !strcmp(core.name, want);
	for (int i=0;i<(int)sizeof(core.guard);i++) if (core.guard[i]!='G') ok = 0; // nothing written past name
	printf("%s  %-52.52s -> \"%.40s\"\n", ok?"ok  ":"FAIL", in, core.name);
	if (!ok) fails++;
}
int main(void) {
	check("/storage/roms/.system/rgb30/cores/gambatte_libretro.so", "gambatte");
	check("/x/mednafen_pce_fast_libretro.so", "mednafen_pce_fast");
	check("snes9x2005_plus_libretro.so", "snes9x2005_plus");
	check("/x/mycore.so", "mycore.so"); // no underscore: crashed
	char longname[400]; memset(longname, 'a', 399); longname[399] = 0;
	char path[512]; snprintf(path, sizeof(path), "/x/%s_libretro.so", longname);
	char want[200]; memset(want, 'a', 127); want[127] = 0; // cut to what fits (127 + the end)
	check(path, want);
	return fails!=0;
}
