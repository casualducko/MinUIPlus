// Turbo_apply (minarch.c): a turbo button held is pressed and released over and over
#include <stdio.h>
#include <string.h>
#include "gen/turbo.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
// the pattern of bit `bit` over n frames of holding buttons `held_buttons` (as "1100...")
static void pattern(int held_buttons, int mask, int half, int bit, int n, char* out) {
	int held[16] = {0};
	for (int i=0; i<n; i++) out[i] = (Turbo_apply(held_buttons, mask, half, held) >> bit) & 1 ? '1' : '0';
	out[n] = '\0';
}
int main(void) {
	char p[64];
	int held[16] = {0};
	CHECK(Turbo_apply(0x0F, 0, 2, held)==0x0F, "no turbo buttons: nothing changes");
	pattern(1<<0, 1<<0, 2, 0, 12, p);
	CHECK(!strcmp(p, "110011001100"), "two frames on, two off (15 presses a second)");
	pattern(1<<0, 1<<0, 1, 0, 8, p);
	CHECK(!strcmp(p, "10101010"), "one on, one off (30 a second)");
	pattern(1<<0, 1<<0, 4, 0, 16, p);
	CHECK(!strcmp(p, "1111000011110000"), "four on, four off (7.5 a second)");
	pattern(1<<0, 1<<0, 0, 0, 6, p);
	CHECK(!strcmp(p, "101010"), "a speed of 0 is treated as 1");
	pattern(1<<0, 1<<0, -3, 0, 4, p);
	CHECK(!strcmp(p, "1010"), "so is a negative one");
	pattern(1<<0, 1<<1, 2, 0, 6, p);
	CHECK(!strcmp(p, "111111"), "a button that is not a turbo button is held as it is");
	pattern((1<<0)|(1<<1), 1<<0, 2, 1, 6, p);
	CHECK(!strcmp(p, "111111"), "another button held with a turbo one is left alone");
	pattern((1<<0)|(1<<1), 1<<0, 2, 0, 6, p);
	CHECK(!strcmp(p, "110011"), "and the turbo one still repeats");

	// releasing starts over: pressed again, it begins with on
	int h[16] = {0};
	int a = Turbo_apply(1, 1, 2, h), b = Turbo_apply(1, 1, 2, h), c = Turbo_apply(1, 1, 2, h); // 1 1 0
	int released = Turbo_apply(0, 1, 2, h);
	int again = Turbo_apply(1, 1, 2, h);
	CHECK(a==1 && b==1 && c==0 && released==0 && again==1, "released in an off frame, pressed again: on at once");

	// two turbo buttons pressed at different times have their own phase
	int h2[16] = {0};
	char x[16], y[16];
	for (int i=0; i<8; i++) {
		int buttons = (1<<0) | (i>=1 ? (1<<1) : 0); // button 1 joins one frame later
		int out = Turbo_apply(buttons, 3, 2, h2);
		x[i] = (out>>0)&1 ? '1' : '0';
		y[i] = (i>=1 && ((out>>1)&1)) ? '1' : (i>=1 ? '0' : '-');
	}
	x[8] = y[8] = '\0';
	CHECK(!strcmp(x, "11001100") && !strcmp(y, "-1100110"), "each button has its own rhythm from when it was pressed");

	int held3[16] = {0};
	int big = 0;
	for (int i=0; i<200000; i++) big = Turbo_apply(1, 1, 2, held3);
	CHECK(held3[0]>0 && (big==1 || big==0), "holding for a very long time is fine");
	CHECK(Turbo_apply(0xFFFF, 0xFFFF, 2, (int[16]){0})==0xFFFF, "all sixteen buttons: the first frame is on for every one");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
