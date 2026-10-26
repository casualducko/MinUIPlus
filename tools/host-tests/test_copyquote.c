// copyFile() and shellQuote() from utils.c: the helpers the downloading tools share
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "gen/copyquote.inc"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("ok    %s\n", msg); else { printf("FAIL  %s\n", msg); fails++; } } while (0)
static long size_of(const char* p) { struct stat st; return stat(p, &st)==0 ? (long)st.st_size : -1; }
int main(void) {
	char q[64];
	shellQuote("/Roms/GBA/Game.gba", q, sizeof(q));
	CHECK(!strcmp(q, "'/Roms/GBA/Game.gba'"), "a plain path is wrapped in single quotes");
	shellQuote("it's", q, sizeof(q));
	CHECK(!strcmp(q, "'it'\\''s'"), "a single quote becomes '\\'' ");
	shellQuote("a b; rm -rf /", q, sizeof(q));
	CHECK(!strcmp(q, "'a b; rm -rf /'"), "spaces and shell characters stay inside the quotes");
	shellQuote("0123456789012345678901234567890123456789", q, 16);
	CHECK(strlen(q)<16 && q[strlen(q)-1]=='\'', "a small buffer is never overrun and the quote is closed");
	shellQuote("x", q, 2);
	CHECK(q[0]=='\0', "a buffer too small for anything: empty");

	const char* from = "/tmp/copyquote-from";
	const char* to = "/tmp/copyquote-to";
	FILE* f = fopen(from, "wb");
	for (int i=0; i<100000; i++) fputc(i & 255, f);
	fclose(f);
	unlink(to);
	CHECK(copyFile(from, to)==0 && size_of(to)==100000, "a file (bigger than the buffer) is copied whole");
	CHECK(copyFile("/tmp/copyquote-no-such-file", to)==-1, "a missing source: -1");
	CHECK(copyFile(from, "/nonexistent-folder/x")==-1, "a destination that can't be written: -1");
	unlink(from); unlink(to);
	printf("%s\n", fails ? "SOME FAILED" : "all passed");
	return fails != 0;
}
