// .cht parsing and the remembered on/off state (Cheat_* in minarch.c)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gen/cheats.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static int parse(const char* text, Cheat* c, int max) {
	char* buf = strdup(text); int n = Cheat_parse(buf, c, max); free(buf); return n;
}
int main(void) {
	static Cheat c[CHEAT_MAX];
	int n = parse("cheats = 3\n\ncheat0_desc = \"Infinite Lives\"\ncheat0_code = \"SXIOPO\"\ncheat0_enable = false\n"
		"cheat1_desc = \"Max Coins\"\ncheat1_code = \"AAA+BBB\"\ncheat1_enable = true\r\n"
		"cheat2_desc = \"No code here\"\n", c, CHEAT_MAX);
	CHECK(n==2, "two cheats with codes (the third has none)");
	CHECK(!strcmp(c[0].desc, "Infinite Lives") && !strcmp(c[0].code, "SXIOPO") && !c[0].enabled, "first, quotes removed, off");
	CHECK(!strcmp(c[1].desc, "Max Coins") && !strcmp(c[1].code, "AAA+BBB") && c[1].enabled, "second, + kept in the code, on (CRLF line)");
	n = parse("cheat5_code = \"X\"\ncheat2_desc = \"Two\"\ncheat2_code=\"Y\"\ncheat5_desc=\"Five\"\n", c, CHEAT_MAX);
	CHECK(n==2 && !strcmp(c[0].desc, "Two") && !strcmp(c[1].desc, "Five"), "gaps in the numbering are closed up, any line order");
	n = parse("cheat0_code = \"X\"\n", c, CHEAT_MAX);
	CHECK(n==1 && !strcmp(c[0].desc, "Cheat 1"), "no description gets a name");
	n = parse("cheat9999_code = \"X\"\ncheat7_code = \"Y\"\n", c, 8);
	CHECK(n==1, "an index past the limit is skipped");
	CHECK(parse("", c, CHEAT_MAX)==0 && parse("garbage\n=\ncheat=1\ncheatx_code=1\n", c, CHEAT_MAX)==0, "empty and junk files");
	char longcode[700]; memset(longcode, 'A', 600); longcode[600] = 0;
	char big[900]; snprintf(big, sizeof(big), "cheat0_code = \"%s\"\n", longcode);
	CHECK(parse(big, c, CHEAT_MAX)==0, "a code too long for the buffer is dropped");
	char desc[300]; memset(desc, 'd', 200); desc[200] = 0;
	snprintf(big, sizeof(big), "cheat0_desc = \"%s\"\ncheat0_code=\"X\"\n", desc);
	CHECK(parse(big, c, CHEAT_MAX)==1 && strlen(c[0].desc)==CHEAT_DESC_MAX-1, "a long description is cut");

	n = parse("cheat0_desc=\"A\"\ncheat0_code=\"1\"\ncheat0_enable=true\ncheat1_desc=\"B\"\ncheat1_code=\"2\"\ncheat2_desc=\"B\"\ncheat2_code=\"3\"\n", c, CHEAT_MAX);
	char state[512], copy[512];
	Cheat_writeState(c, n, state, sizeof(state));
	CHECK(!strcmp(state, "0\tA\n"), "state lists only what is on");
	c[2].enabled = 1;
	Cheat_writeState(c, n, state, sizeof(state));
	CHECK(!strcmp(state, "0\tA\n2\tB\n"), "state after turning the third on");
	strcpy(copy, state);
	Cheat_readState(copy, c, n);
	CHECK(c[0].enabled && !c[1].enabled && c[2].enabled && n==3, "reading it back: the number tells the two B cheats apart");
	strcpy(copy, "");
	Cheat_readState(copy, c, n);
	CHECK(!c[0].enabled && !c[1].enabled && !c[2].enabled, "an empty state turns everything off (it overrides the file's flags)");
	strcpy(copy, "9\tGone\n0\tA\n");
	Cheat_readState(copy, c, n);
	CHECK(c[0].enabled && !c[1].enabled, "unknown names are ignored");
	strcpy(copy, "5\tA\n");
	Cheat_readState(copy, c, n);
	CHECK(c[0].enabled && !c[1].enabled && !c[2].enabled, "a wrong number falls back to the description");
	strcpy(copy, "1\tB\n1\tB\n");
	Cheat_readState(copy, c, n);
	CHECK(c[1].enabled && c[2].enabled && !c[0].enabled, "the same line twice fills the next one with that name");
	strcpy(copy, "no tab here\n\n");
	Cheat_readState(copy, c, n);
	CHECK(!c[0].enabled && !c[1].enabled && !c[2].enabled, "lines without a tab are ignored");
	char tiny[6];
	c[0].enabled = c[1].enabled = c[2].enabled = 1;
	Cheat_writeState(c, n, tiny, sizeof(tiny));
	CHECK(strlen(tiny) < sizeof(tiny), "a small buffer is respected");
	return fails ? 1 : 0;
}
