// splitVersion() from info_view.c: the version screen's reading of .system/version.txt.
// It used to strrchr() for newlines and write through the result: a missing or one-line file crashed the launcher.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void trimTrailingNewlines(char* line) { size_t n = strlen(line); while (n>0 && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n] = '\0'; }
#include "gen/splitVersion.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static void run(const char* text, char* rel, char** commit, char* buf) { snprintf(buf, 256, "%s", text); splitVersion(buf, commit); (void)rel; }
int main(void) {
	char buf[256], *commit;
	run("MinUI-20251127-1\ndbf8943\n", NULL, &commit, buf);
	CHECK(!strcmp(buf, "MinUI-20251127-1") && !strcmp(commit, "dbf8943"), "the stock file");
	run("MinUI-20251127-1\r\ndbf8943\r\n", NULL, &commit, buf);
	CHECK(!strcmp(buf, "MinUI-20251127-1") && !strcmp(commit, "dbf8943"), "CRLF");
	run("only a release", NULL, &commit, buf);
	CHECK(!strcmp(buf, "only a release") && !strcmp(commit, ""), "one line, no newline: commit blank (used to crash)");
	run("", NULL, &commit, buf);
	CHECK(!strcmp(buf, "") && !strcmp(commit, ""), "empty file (a missing one reads as empty): blanks (used to crash)");
	run("release\n", NULL, &commit, buf);
	CHECK(!strcmp(buf, "release") && !strcmp(commit, ""), "one line with its newline");
	run("a\nb\nc\nd\n", NULL, &commit, buf);
	CHECK(!strcmp(buf, "a") && !strcmp(commit, "b"), "extra lines are ignored");
	run("\n\n", NULL, &commit, buf);
	CHECK(!strcmp(buf, "") && !strcmp(commit, ""), "only newlines");
	printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
	return fails != 0;
}
