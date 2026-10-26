// The Switcher tool's logic (workspace/all/switcher/switcher_core.c): the recent list, slots, times
#include <stdio.h>
#include <string.h>
#include "switcher_core.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("ok    %s\n", msg); else { printf("FAIL  %s\n", msg); fails++; } } while (0)

int main(void) {
	char paths[8][SW_PATH_MAX];
	char text[] = "/Roms/GBA (MGBA)/Apotris.gba\n/Roms/Playstation (PS)/Game.m3u\tAlias\n\n   \r\n/Roms/NES (FC)/Mega Man (USA).nes\r\n";
	int n = SW_parseRecents(text, paths, 8);
	CHECK(n==3 && !strcmp(paths[0], "/Roms/GBA (MGBA)/Apotris.gba") && !strcmp(paths[1], "/Roms/Playstation (PS)/Game.m3u") && !strcmp(paths[2], "/Roms/NES (FC)/Mega Man (USA).nes"), "paths, newest first; an alias, blank lines and CRLF are dealt with");
	char two[] = "/a.gba\n/b.gba\n/c.gba\n";
	CHECK(SW_parseRecents(two, paths, 2)==2, "no more than max");
	char none[] = "";
	CHECK(SW_parseRecents(none, paths, 8)==0, "an empty file: none");

	char out[512];
	const char* list = "/Roms/A/one.gba\n/Roms/A/two.gba\tTwo\n/Roms/A/three.gba\n";
	int len = SW_dropRecent(list, "/Roms/A/two.gba", out, sizeof(out));
	CHECK(len==(int)strlen(out) && !strcmp(out, "/Roms/A/one.gba\n/Roms/A/three.gba\n"), "one game is dropped, its alias with it, the others stay in order");
	len = SW_dropRecent(list, "/Roms/A/nothing.gba", out, sizeof(out));
	CHECK(!strcmp(out, list), "a game that isn't there: the list is unchanged");
	len = SW_dropRecent(list, "/Roms/A/one", out, sizeof(out));
	CHECK(!strcmp(out, list), "only a whole path matches");
	len = SW_dropRecent("/x.gba\n", "/x.gba", out, sizeof(out));
	CHECK(len==0 && out[0]=='\0', "the last one dropped: empty");
	len = SW_dropRecent("/Roms/A/one.gba \n/Roms/A/two.gba\n", "/Roms/A/one.gba", out, sizeof(out));
	CHECK(!strcmp(out, "/Roms/A/two.gba\n"), "a path with trailing spaces (the parser trims them) is dropped too");
	len = SW_dropRecent(list, "/Roms/A/one.gba", out, 8);
	CHECK(len<8, "a small buffer is never overrun");

	CHECK(SW_parseSlot("3\n")==3 && SW_parseSlot("0")==0 && SW_parseSlot(" 9 ")==9, "a slot number, whitespace around it ignored");
	CHECK(SW_parseSlot("")==-1 && SW_parseSlot("x")==-1 && SW_parseSlot("12")==-1 && SW_parseSlot("-1")==-1 && SW_parseSlot("3x")==-1 && SW_parseSlot(NULL)==-1, "anything else: -1");

	CHECK(SW_pickSlot(2, 1000, 0)==2 && SW_pickSlot(-1, 0, 0)==-1, "no auto-save: the slot you saved to, or none");
	CHECK(SW_pickSlot(-1, 0, 500)==9, "only an auto-save: it");
	CHECK(SW_pickSlot(2, 1000, 2000)==9 && SW_pickSlot(2, 3000, 2000)==2, "both: the newer picture wins");
	CHECK(SW_pickSlot(2, 0, 2000)==9 && SW_pickSlot(3, 1000, 1000)==3, "a manual slot with no picture loses to an auto-save; a tie keeps the manual one");
	CHECK(SW_pickSlot(9, 500, 0)==-1 && SW_pickSlot(12, 500, 0)==-1, "slot 9 is not a manual slot (and out of range is no slot)");
	char s[64];
	SW_ago(5, s, sizeof(s)); CHECK(!strcmp(s, "just now"), "seconds");
	SW_ago(60, s, sizeof(s)); CHECK(!strcmp(s, "1 minute ago"), "one minute");
	SW_ago(300, s, sizeof(s)); CHECK(!strcmp(s, "5 minutes ago"), "minutes");
	SW_ago(3*3600+5, s, sizeof(s)); CHECK(!strcmp(s, "3 hours ago"), "hours");
	SW_ago(2*86400+9, s, sizeof(s)); CHECK(!strcmp(s, "2 days ago"), "days");
	SW_ago(40L*86400, s, sizeof(s)); CHECK(!strcmp(s, "1 month ago"), "a month");
	SW_ago(400L*86400, s, sizeof(s)); CHECK(!strcmp(s, "1 year ago"), "a year");
	SW_ago(-30, s, sizeof(s)); CHECK(!strcmp(s, "just now"), "a clock that is behind: just now");

	CHECK(!strcmp(SW_baseName("/Roms/GBA (MGBA)/Apotris.gba"), "Apotris.gba") && !strcmp(SW_baseName("plain"), "plain"), "base name");
	printf("%s\n", fails ? "SOME FAILED" : "all passed");
	return fails ? 1 : 0;
}
