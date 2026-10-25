// the screenshot gallery's names and order (gallery_core.c)
#include <stdio.h>
#include <string.h>
#include "gallery_core.c"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static char game[256], when[64];
int main(void) {
	CHECK(Shots_parse("Zelda - Link 20261024-223105.png", game, sizeof(game), when, sizeof(when)) && !strcmp(game, "Zelda - Link") && !strcmp(when, "2026-10-24 22:31:05"), "game and time");
	CHECK(Shots_parse("Mega Man 20261024-223105-2.png", game, sizeof(game), when, sizeof(when)) && !strcmp(game, "Mega Man") && !strcmp(when, "2026-10-24 22:31:05"), "the -2 of a second shot is not part of the time");
	CHECK(Shots_parse("Game 20261024-223105.BMP", game, sizeof(game), when, sizeof(when)) && !strcmp(game, "Game"), "a bmp, in capitals");
	CHECK(Shots_parse("Pokemon - The Pit - Gen 3 (v2.5.1) 20260102-030405.png", game, sizeof(game), when, sizeof(when)) && !strcmp(game, "Pokemon - The Pit - Gen 3 (v2.5.1)"), "dashes and parentheses in the title");
	CHECK(!Shots_parse("holiday.png", game, sizeof(game), when, sizeof(when)) && !strcmp(game, "holiday") && when[0]=='\0', "no stamp: the name");
	CHECK(!Shots_parse("Game 2026102-223105.png", game, sizeof(game), when, sizeof(when)) && !strcmp(game, "Game 2026102-223105"), "a short date is not a stamp");
	CHECK(!Shots_parse("Game 20261024-22310x.png", game, sizeof(game), when, sizeof(when)), "letters in the time");
	CHECK(!Shots_parse("Game 20261024-223105-.png", game, sizeof(game), when, sizeof(when)), "a dash with no number");
	CHECK(Shots_parse("20261024-223105.png", game, sizeof(game), when, sizeof(when))==0, "only a stamp, no game name: not parsed (there is no space before it)");
	CHECK(Shots_parse("Game  20261024-223105.png", game, sizeof(game), when, sizeof(when)) && !strcmp(game, "Game"), "two spaces before the stamp");
	CHECK(Shots_parse("", game, sizeof(game), when, sizeof(when))==0 && game[0]=='\0', "empty name");
	char tiny[8], w2[8];
	Shots_parse("A Very Long Game Name 20261024-223105.png", tiny, sizeof(tiny), w2, sizeof(w2));
	CHECK(strlen(tiny)<sizeof(tiny) && strlen(w2)<sizeof(w2), "small buffers");

	CHECK(Shots_compare("A 20261025-000000.png", "B 20261024-235959.png")<0, "a later date comes first");
	CHECK(Shots_compare("A 20261024-223106.png", "A 20261024-223105.png")<0, "a later second comes first");
	CHECK(Shots_compare("A 20261024-223105-2.png", "A 20261024-223105.png")<0, "the second shot of a second is newer");
	CHECK(Shots_compare("A 20261024-223105-10.png", "A 20261024-223105-9.png")<0, "-10 is after -9");
	CHECK(Shots_compare("A 20261024-223105.png", "zzz.png")<0 && Shots_compare("zzz.png", "A 20261024-223105.png")>0, "stamped before unstamped");
	CHECK(Shots_compare("a.png", "b.png")<0 && Shots_compare("b.png", "a.png")>0 && Shots_compare("a.png", "a.png")==0, "unstamped by name");
	CHECK(Shots_compare("A 20261024-223105.png", "B 20261024-223105.png")<0, "the same moment: by name");
	const char* list[] = {"old 20250101-000000.png", "x.png", "new 20261231-235959.png", "mid 20260601-120000.png", "mid 20260601-120000-2.png"};
	char* order[5];
	for (int i=0; i<5; i++) order[i] = (char*)list[i];
	for (int i=0; i<5; i++) for (int j=i+1; j<5; j++) if (Shots_compare(order[i], order[j])>0) { char* t = order[i]; order[i] = order[j]; order[j] = t; }
	CHECK(!strcmp(order[0], "new 20261231-235959.png") && !strcmp(order[1], "mid 20260601-120000-2.png") && !strcmp(order[2], "mid 20260601-120000.png") && !strcmp(order[3], "old 20250101-000000.png") && !strcmp(order[4], "x.png"), "a list sorted newest first");

	CHECK(Shots_isImage("a.png") && Shots_isImage("a.PNG") && Shots_isImage("a.bmp"), "images");
	CHECK(!Shots_isImage(".hidden.png") && !Shots_isImage("a.png.part") && !Shots_isImage("a.txt") && !Shots_isImage("noext") && !Shots_isImage(NULL), "not images");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
