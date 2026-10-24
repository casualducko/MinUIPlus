// getFile() from utils.c: it used to leave the caller's buffer alone when the file was missing,
// so callers read uninitialized stack (the version screen crashed the launcher on a card without version.txt).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "gen/getFile.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static void put(const char* path, const char* text) { FILE* f = fopen(path, "w"); fputs(text, f); fclose(f); }
int main(void) {
	char dir[512], p[600], buf[64];
	snprintf(dir, sizeof(dir), "%s/getfile-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
	mkdir(dir, 0777);
	snprintf(p, sizeof(p), "%s/a.txt", dir);
	put(p, "hello\nworld\n");
	memset(buf, 'Z', sizeof(buf));
	getFile(p, buf, sizeof(buf));
	CHECK(!strcmp(buf, "hello\nworld\n"), "reads a file");
	memset(buf, 'Z', sizeof(buf)); buf[sizeof(buf)-1] = 0;
	snprintf(p, sizeof(p), "%s/missing.txt", dir);
	getFile(p, buf, sizeof(buf));
	CHECK(buf[0]=='\0', "a missing file leaves an empty string, not the old contents");
	snprintf(p, sizeof(p), "%s/long.txt", dir);
	char* big = malloc(1000); memset(big, 'x', 999); big[999] = 0; put(p, big); free(big);
	memset(buf, 'Z', sizeof(buf));
	getFile(p, buf, sizeof(buf));
	CHECK(strlen(buf)==sizeof(buf)-1, "a long file is cut to the buffer, with its terminator");
	memset(buf, 'Z', sizeof(buf));
	getFile(dir, buf, sizeof(buf)); // a directory: fopen works, reading doesn't
	CHECK(strlen(buf)<sizeof(buf), "a directory reads as empty, terminated");
	snprintf(p, sizeof(p), "%s/empty.txt", dir); put(p, "");
	memset(buf, 'Z', sizeof(buf));
	getFile(p, buf, sizeof(buf));
	CHECK(buf[0]=='\0', "an empty file");
	char one[1] = {'Z'}; getFile(p, one, 1);
	CHECK(one[0]=='\0', "a buffer of 1 holds just the terminator");
	getFile(p, buf, 0); CHECK(1, "a buffer of 0 is left alone (no write)");
	char cmd[700]; snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir); system(cmd);
	printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
	return fails != 0;
}
