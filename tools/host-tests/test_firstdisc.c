// getFirstDisc() from minui.c: the first disc named in an .m3u, as a full path.
// Regression: it once cut the path to 7 characters (sizeof of a pointer parameter).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#define MAX_PATH 1024
// the helpers it uses, as in utils.c
static int exists(char* path) { return access(path, F_OK)==0; }
static void normalizeNewline(char* line) { size_t n = strlen(line); if (n>1 && line[n-1]=='\n' && line[n-2]=='\r') { line[n-2] = '\n'; line[n-1] = '\0'; } }
static void trimTrailingNewlines(char* line) { size_t n = strlen(line); while (n>0 && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n] = '\0'; }
#include "gen/getFirstDisc.inc"

static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static void put(const char* path, const char* text) { FILE* f = fopen(path, "w"); fputs(text, f); fclose(f); }

int main(void) {
	char root[512];
	snprintf(root, sizeof(root), "%s/firstdisc-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
	mkdir(root, 0777);
	char dir[900];
	snprintf(dir, sizeof(dir), "%s/A game with a long folder name (Disc 1 and 2) (Europe) (En,Fr,De,Es,It)", root);
	mkdir(dir, 0777);
	char m3u[1000], disc1[1000], disc2[1000], got[MAX_PATH];
	snprintf(m3u, sizeof(m3u), "%s/Game.m3u", dir);
	snprintf(disc1, sizeof(disc1), "%s/Game (Disc 1).chd", dir);
	snprintf(disc2, sizeof(disc2), "%s/Game (Disc 2).chd", dir);
	put(disc1, "x"); put(disc2, "y");
	
	put(m3u, "Game (Disc 1).chd\nGame (Disc 2).chd\n");
	memset(got, 'Z', sizeof(got)); got[sizeof(got)-1] = 0;
	CHECK(getFirstDisc(m3u, got)==1, "finds the first disc");
	CHECK(!strcmp(got, disc1), "the whole path, not cut short (path is over 100 characters)");
	CHECK(strlen(got)>100, "long enough to have caught the 7 character bug");
	
	put(m3u, "\r\n\r\nGame (Disc 2).chd\r\nGame (Disc 1).chd\r\n");
	got[0] = 0;
	CHECK(getFirstDisc(m3u, got)==1 && !strcmp(got, disc2), "skips empty lines, CRLF line endings, takes the first named");
	
	put(m3u, "Missing.chd\n");
	got[0] = 0;
	CHECK(getFirstDisc(m3u, got)==0, "a first disc that doesn't exist is not found");
	put(m3u, "");
	CHECK(getFirstDisc(m3u, got)==0, "empty m3u: not found");
	char* big = malloc(5000); memset(big, 'a', 4999); big[4999] = 0;
	char content[6000]; snprintf(content, sizeof(content), "%s\n", big);
	put(m3u, content); got[0] = 0;
	CHECK(getFirstDisc(m3u, got)==0 && strlen(got)<MAX_PATH, "a 5000 character line: cut to the buffer, not found, no overflow (ASan)");
	free(big);
	
	char cmd[1200]; snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root); system(cmd);
	printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
	return fails != 0;
}
