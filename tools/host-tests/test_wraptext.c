// GFX_wrapText() from api.c. Normal text must wrap exactly as it always did
// (wraptext.golden.tsv was captured from the original function), and a first word
// that is wider than the room, which used to dereference NULL, must not crash.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define MAX_PATH 1024
typedef struct TTF_Font TTF_Font;
static int TTF_SizeUTF8(TTF_Font* f, const char* t, int* w, int* h) {
	int n=0; for (const unsigned char* p=(const unsigned char*)t; *p; p++) if ((*p&0xC0)!=0x80) n++;
	if (w) *w = n*10; return 0;
}
static int GFX_textWidth(TTF_Font* f, const char* t) { int w = 0; TTF_SizeUTF8(f, t, &w, NULL); return w; } // no fallback font in the tests
#include "gen/GFX_truncate.inc"
#include "gen/GFX_wrapText.inc"

static int fails = 0;
int main(int argc, char** argv) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/wraptext.golden.tsv", argc>1 ? argv[1] : ".");
	FILE* file = fopen(path, "r");
	if (!file) { printf("FAIL  can't open %s\n", path); return 1; }
	char line[2048]; int rows = 0, bad = 0;
	while (fgets(line, sizeof(line), file)) {
		line[strcspn(line, "\n")] = 0;
		char* f[6]; int n = 0;
		for (char* p = strtok(line, "\t"); p && n<6; p = strtok(NULL, "\t")) f[n++] = p;
		if (n!=6) continue;
		int width = atoi(f[1]), lines = atoi(f[2]), want_w = atoi(f[4]);
		char buf[512]; snprintf(buf, sizeof(buf), "%s", f[3]);
		int got_w = GFX_wrapText(NULL, buf, width, lines);
		for (char* c = buf; *c; c++) if (*c=='\n') *c = '|';
		rows++;
		if (got_w!=want_w || strcmp(buf, f[5])) { bad++; printf("FAIL  \"%s\" width %d lines %d -> %d \"%s\" (was %d \"%s\")\n", f[3], width, lines, got_w, buf, want_w, f[5]); }
	}
	fclose(file);
	printf("%s  %d normal cases wrap exactly as before\n", bad?"FAIL":"ok  ", rows);
	fails += bad!=0 || rows==0;

	// a first word wider than the room, followed by a space (NULL dereference before)
	const char* hard[] = {
		"Supercalifragilisticexpialidocious and some more words after it",
		"https://example.com/a/very/long/unbroken/address and a description",
		"x_______________________________________ y z", NULL };
	for (int i=0; hard[i]; i++) {
		for (int lines=0; lines<=7; lines+=7) {
			char buf[512]; snprintf(buf, sizeof(buf), "%s", hard[i]);
			GFX_wrapText(NULL, buf, 100, lines);
		}
	}
	printf("ok    a first word wider than the room doesn't crash\n");
	return fails!=0;
}
