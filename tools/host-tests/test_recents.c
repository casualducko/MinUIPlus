// the recently played list update (Recents_bump in utils.c)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gen/recents.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static char out[4096];
static const char* bump(const char* text, const char* path, int max) { Recents_bump(text, path, max, out, sizeof(out)); return out; }
int main(void) {
	CHECK(!strcmp(bump(NULL, "/Roms/A/x.gb", 24), "/Roms/A/x.gb\n"), "no list yet: one line");
	CHECK(!strcmp(bump("", "/Roms/A/x.gb", 24), "/Roms/A/x.gb\n"), "an empty file");
	CHECK(!strcmp(bump("/Roms/B/b.gb\n/Roms/C/c.gb\n", "/Roms/A/x.gb", 24), "/Roms/A/x.gb\n/Roms/B/b.gb\n/Roms/C/c.gb\n"), "a new game goes on top");
	CHECK(!strcmp(bump("/Roms/A/x.gb\n/Roms/B/b.gb\n", "/Roms/A/x.gb", 24), "/Roms/A/x.gb\n/Roms/B/b.gb\n"), "already on top: no duplicate");
	CHECK(!strcmp(bump("/Roms/B/b.gb\n/Roms/C/c.gb\n/Roms/A/x.gb\n/Roms/D/d.gb\n", "/Roms/A/x.gb", 24), "/Roms/A/x.gb\n/Roms/B/b.gb\n/Roms/C/c.gb\n/Roms/D/d.gb\n"), "further down: moved to the top, the rest in order");
	CHECK(!strcmp(bump("/Roms/B/b.gb\t(Collection)\n/Roms/A/x.gb\tAlias\n", "/Roms/A/x.gb", 24), "/Roms/A/x.gb\tAlias\n/Roms/B/b.gb\t(Collection)\n"), "its alias goes with it, other aliases stay");
	CHECK(!strcmp(bump("/Roms/B/b.gb\n/Roms/C/c.gb\n/Roms/D/d.gb\n", "/Roms/A/x.gb", 3), "/Roms/A/x.gb\n/Roms/B/b.gb\n/Roms/C/c.gb\n"), "a full list drops the oldest");
	CHECK(!strcmp(bump("/Roms/B/b.gb\n/Roms/A/x.gb\n/Roms/D/d.gb\n", "/Roms/A/x.gb", 3), "/Roms/A/x.gb\n/Roms/B/b.gb\n/Roms/D/d.gb\n"), "a full list with the game in it drops nothing");
	CHECK(!strcmp(bump("/Roms/B/b.gb\r\n/Roms/C/c.gb\r\n", "/Roms/A/x.gb", 24), "/Roms/A/x.gb\n/Roms/B/b.gb\n/Roms/C/c.gb\n"), "Windows line endings are cleaned");
	CHECK(!strcmp(bump("/Roms/A/x.gb2\n/Roms/A/x.g\n", "/Roms/A/x.gb", 24), "/Roms/A/x.gb\n/Roms/A/x.gb2\n/Roms/A/x.g\n"), "paths that only start the same are different games");
	CHECK(!strcmp(bump("\n/Roms/B/b.gb\n\n\n", "/Roms/A/x.gb", 24), "/Roms/A/x.gb\n/Roms/B/b.gb\n"), "empty lines are dropped");
	CHECK(!strcmp(bump("/Roms/B/b.gb", "/Roms/A/x.gb", 24), "/Roms/A/x.gb\n/Roms/B/b.gb\n"), "a last line without a newline");
	char small[16];
	size_t n = Recents_bump("/Roms/B/b.gb\n/Roms/C/c.gb\n", "/Roms/A/x.gb", 24, small, sizeof(small));
	CHECK(n<sizeof(small) && strlen(small)==n, "a small buffer is cut and terminated");
	CHECK(Recents_bump("a\n", "b", 24, small, 0)==0, "no buffer");
	CHECK(!strcmp(bump("/Roms/B/b.gb\n", "/Roms/A/x.gb", 1), "/Roms/A/x.gb\n"), "a list of one");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
