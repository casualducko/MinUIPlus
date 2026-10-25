// the title art logic (titleart_core.c): title cleanup, wrapping, layout, packing
#include <stdio.h>
#include <string.h>
#include "titleart_core.c"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static char out[512], sub[512];
static const char* title(const char* name) { TA_title(name, out, sizeof(out)); return out; }
static float fake(const char* line, void* ctx) { (void)ctx; return 0.62f * strlen(line); } // 0.62 wide per letter
static float fat(const char* line, void* ctx) { (void)ctx; return 1.2f * strlen(line); } // wide letters
int main(void) {
	CHECK(!strcmp(title("Legend of Zelda, The (USA) (Rev 1).sfc"), "The Legend of Zelda"), "tags dropped, \", The\" moved to the front");
	CHECK(!strcmp(title("Apotris.gba"), "Apotris"), "extension dropped");
	CHECK(!strcmp(title("Super Mario Bros. (World).nes"), "Super Mario Bros."), "a dot in the title stays");
	CHECK(!strcmp(title("Mr. Do!.nes"), "Mr. Do!"), "only the real extension goes");
	CHECK(!strcmp(title("Dr. Mario"), "Dr. Mario"), "no extension, a dot and a space");
	CHECK(!strcmp(title("Game [!].gb"), "Game"), "[tags]");
	CHECK(!strcmp(title("Boy and His Blob, A (USA).nes"), "A Boy and His Blob"), "\", A\"");
	CHECK(!strcmp(title("Tetris"), "Tetris"), "plain");
	CHECK(!strcmp(title("  Spaced   (x).gb"), "Spaced"), "leading and trailing space");
	CHECK(!strcmp(title("Pokemon Heart and Soul (v1.2.1).gba"), "Pokemon Heart and Soul"), "version tags");
	TA_title("Long Name Long Name Long Name Long Name", out, 10);
	CHECK(strlen(out)==9, "a small buffer is cut");

	char m[256];
	TA_split("Castlevania - Symphony of the Night", m, sizeof(m), sub, sizeof(sub));
	CHECK(!strcmp(m, "Castlevania") && !strcmp(sub, "Symphony of the Night"), "split at \" - \"");
	TA_split("Zelda: Link's Awakening", m, sizeof(m), sub, sizeof(sub));
	CHECK(!strcmp(m, "Zelda") && !strcmp(sub, "Link's Awakening"), "split at \": \"");
	TA_split("Plain Title", m, sizeof(m), sub, sizeof(sub));
	CHECK(!strcmp(m, "Plain Title") && sub[0]=='\0', "no subtitle");
	TA_split("Dangling - ", m, sizeof(m), sub, sizeof(sub));
	CHECK(!strcmp(m, "Dangling - ") && sub[0]=='\0', "nothing after the separator: no split");
	TA_split("A - B - C", m, sizeof(m), sub, sizeof(sub));
	CHECK(!strcmp(m, "A") && !strcmp(sub, "B - C"), "the first separator");

	char lines[TA_MAX_LINES][TA_LINE_MAX];
	CHECK(TA_wrapWords("MEGA MAN X", 2, lines)==2 && !strcmp(lines[0], "MEGA") && !strcmp(lines[1], "MAN X"), "wrap in two, as even as possible");
	CHECK(TA_wrapWords("MEGA MAN X", 1, lines)==1 && !strcmp(lines[0], "MEGA MAN X"), "wrap in one");
	CHECK(TA_wrapWords("MEGA MAN", 3, lines)==0, "more lines than words");
	CHECK(TA_wrapWords("ONE TWO THREE FOUR FIVE", 3, lines)==3 && strlen(lines[0])<=9 && strlen(lines[1])<=9 && strlen(lines[2])<=9, "three lines");
	CHECK(TA_wrapWords("", 1, lines)==0, "empty");

	CHECK(TA_wrapPlain("SYMPHONY OF THE NIGHT", 12, 2, lines)==2 && !strcmp(lines[0], "SYMPHONY OF") && !strcmp(lines[1], "THE NIGHT"), "plain wrap");
	CHECK(TA_wrapPlain("ONE TWO THREE FOUR FIVE SIX SEVEN", 10, 2, lines)==2 && lines[1][strlen(lines[1])-1]=='~', "too many lines: the last is cut with ~");
	CHECK(TA_wrapPlain("SHORT", 20, 2, lines)==1 && !strcmp(lines[0], "SHORT"), "one line");
	CHECK(TA_wrapPlain("SUPERCALIFRAGILISTICEXPIALIDOCIOUS", 10, 2, lines)==1 && strlen(lines[0])<=10, "a word wider than the line is cut");
	CHECK(TA_wrapPlain("", 10, 2, lines)==0, "empty plain");

	TA_Layout lay;
	CHECK(TA_layout("Mega Man X", 56, 22, fake, NULL, &lay) && lay.count==2 && lay.h==9 && !strcmp(lay.lines[0], "MEGA") && !strcmp(lay.lines[1], "MAN X"), "layout: two lines of 9 rows win");
	CHECK(TA_layout("Apotris", 56, 22, fake, NULL, &lay) && lay.count==1 && lay.h==6, "one word: one line, as high as the width allows");
	CHECK(TA_layout("Ab", 56, 22, fake, NULL, &lay) && lay.count==1 && lay.h==11, "a very short word: capped at 11 rows");
	CHECK(TA_layout("A B", 56, 22, fake, NULL, &lay) && lay.h>=4, "tiny title");
	CHECK(!TA_layout("Super Mario Land 2 DX Extra Long Title Words Here", 56, 22, fat, NULL, &lay), "too long for 4-row letters: no layout, nothing is cut");
	CHECK(TA_layout("Burgertime Deluxe EX", 56, 22, fake, NULL, &lay) && lay.count==2 && !strcmp(lay.lines[0], "BURGERTIME") && !strcmp(lay.lines[1], "DELUXE EX"), "every word is kept");
	CHECK(TA_layout("", 56, 22, fake, NULL, &lay) && !strcmp(lay.lines[0], "?"), "empty title");
	CHECK(!TA_layout("Pneumonoultramicroscopicsilicovolcanoconiosis", 56, 22, fat, NULL, &lay), "one huge word: no layout (and no endless loop)");
	CHECK(TA_layout("Mega Man X", 56, 12, fake, NULL, &lay) && lay.h<=6, "fewer rows: shorter letters");

	uint8_t px[4*6];
	memset(px, 0, sizeof(px));
	// 4 wide, 3 rows (6 sub rows): column 0 full, column 1 top only, column 2 bottom only, column 3 none
	for (int y=0; y<6; y++) px[y*4+0] = 255;
	for (int r=0; r<3; r++) { px[(2*r)*4+1] = 200; px[(2*r+1)*4+2] = 200; }
	px[1*4+3] = 107; px[0*4+3] = 108; // the threshold: 107 is out, 108 is in
	char packed[3*4];
	TA_pack(px, 4, 3, packed);
	CHECK(packed[0]=='H' && packed[1]=='\'' && packed[2]=='.' && packed[3]=='\'', "pack: both, top, bottom, and 108 is in");
	CHECK(packed[4]=='H' && packed[8]=='H', "pack: every row");

	char b[64];
	CHECK(TA_badge("Genesis (MD)", b, sizeof(b)) && !strcmp(b, "GENESIS"), "badge: the name");
	CHECK(TA_badge("Nintendo Entertainment System (FC)", b, sizeof(b)) && !strcmp(b, "FC"), "badge: a long name gives its tag");
	CHECK(TA_badge("GBA (MGBA)", b, sizeof(b)) && !strcmp(b, "GBA"), "badge: short name");
	CHECK(TA_badge("Roms", b, sizeof(b)) && !strcmp(b, "ROMS"), "badge: no tag");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
