// Random_pick (utils.c): which game the launcher's random button chooses
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gen/random.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
int main(void) {
	uint32_t s = 12345;
	CHECK(Random_pick(NULL, 0, &s)==-1 && Random_pick(NULL, -3, &s)==-1, "no games: -1");
	CHECK(Random_pick(NULL, 1, &s)==0, "one game: that one");
	unsigned char played[10];
	memset(played, 1, sizeof(played));
	played[7] = 0;
	int only = 1;
	for (int i=0; i<200; i++) if (Random_pick(played, 10, &s)!=7) only = 0;
	CHECK(only, "one game unplayed: always that one");
	memset(played, 1, sizeof(played));
	int seen[10] = {0};
	for (int i=0; i<2000; i++) { int r = Random_pick(played, 10, &s); if (r<0 || r>=10) { only = 0; break; } seen[r]++; }
	int all = 1;
	for (int i=0; i<10; i++) if (seen[i]<100) all = 0;
	CHECK(all, "all played: any game, spread evenly (each at least 5%)");
	memset(played, 0, sizeof(played));
	played[0] = played[1] = played[2] = 1;
	memset(seen, 0, sizeof(seen));
	for (int i=0; i<3000; i++) seen[Random_pick(played, 10, &s)]++;
	CHECK(seen[0]==0 && seen[1]==0 && seen[2]==0, "played games are skipped while others remain");
	int spread = 1;
	for (int i=3; i<10; i++) if (seen[i]<250) spread = 0;
	CHECK(spread, "the unplayed ones are chosen evenly");
	uint32_t zero = 0;
	CHECK(Random_pick(NULL, 5, &zero)>=0 && zero!=0, "a zero state is repaired (xorshift would stay at 0)");
	CHECK(Random_pick(NULL, 5, NULL)==-1, "no state");
	uint32_t a = 777, b = 777;
	int same = 1;
	for (int i=0; i<50; i++) if (Random_pick(NULL, 100, &a)!=Random_pick(NULL, 100, &b)) same = 0;
	CHECK(same, "the same state gives the same picks");
	uint32_t c = 1;
	int changes = 0, last = Random_pick(NULL, 1000, &c);
	for (int i=0; i<100; i++) { int r = Random_pick(NULL, 1000, &c); if (r!=last) changes++; last = r; }
	CHECK(changes>90, "it does not repeat itself");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
