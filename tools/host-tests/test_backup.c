// fileEquals() and backupRotate() from utils.c: older versions of a save are kept, newest first
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#define MAX_PATH 1024
static int exists(char* p) { return access(p, F_OK)==0; }
#include "gen/mkdirs.inc"
#include "gen/backup.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static void put(const char* p, const char* s) { FILE* f = fopen(p, "wb"); fwrite(s, 1, strlen(s), f); fclose(f); }
static int has(const char* p, const char* s) { char b[64]={0}; FILE* f = fopen(p, "rb"); if (!f) return 0; size_t n = fread(b,1,63,f); fclose(f); return n==strlen(s) && !memcmp(b,s,n); }
int main(void) {
	char root[512], file[600], dir[600], p[700];
	snprintf(root, sizeof(root), "%s/backup-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
	mkdir(root, 0777);
	snprintf(file, sizeof(file), "%s/Game.sav", root);
	snprintf(dir, sizeof(dir), "%s/.backups", root);

	put(file, "save one");
	CHECK(fileEquals(file, "save one", 8), "fileEquals: same bytes");
	CHECK(!fileEquals(file, "save two", 8), "fileEquals: same length, different bytes");
	CHECK(!fileEquals(file, "save on", 7), "fileEquals: shorter data");
	CHECK(!fileEquals(file, "save one!", 9), "fileEquals: longer data");
	CHECK(!fileEquals("/no/such/file", "x", 1), "fileEquals: missing file");
	CHECK(!fileEquals(root, "x", 1), "fileEquals: a folder is not a save");

	// four saves in a row, keeping three
	struct timeval old[2] = { {1700000000,0}, {1700000000,0} };
	utimes(file, old); // "save one" was written long ago
	CHECK(backupRotate(file, dir, "Game.sav", 3)==0, "first rotation works (and makes the folder)");
	snprintf(p, sizeof(p), "%s/Game.sav.1", dir);
	CHECK(has(p, "save one"), ".1 holds the version that was current");
	struct stat st; stat(p, &st);
	CHECK(st.st_mtime==1700000000, "the copy keeps the original modification time");
	put(file, "save two");   backupRotate(file, dir, "Game.sav", 3);
	put(file, "save three"); backupRotate(file, dir, "Game.sav", 3);
	snprintf(p, sizeof(p), "%s/Game.sav.1", dir); CHECK(has(p, "save three"), ".1 is the newest");
	snprintf(p, sizeof(p), "%s/Game.sav.2", dir); CHECK(has(p, "save two"), ".2 is the one before");
	snprintf(p, sizeof(p), "%s/Game.sav.3", dir); CHECK(has(p, "save one"), ".3 is the oldest");
	put(file, "save four");  backupRotate(file, dir, "Game.sav", 3);
	snprintf(p, sizeof(p), "%s/Game.sav.3", dir); CHECK(has(p, "save two"), "after a fourth, the oldest (one) is dropped");
	snprintf(p, sizeof(p), "%s/Game.sav.4", dir); CHECK(!exists(p), "never more than three");
	snprintf(p, sizeof(p), "%s/Game.sav.new", dir); CHECK(!exists(p), "no temp file left behind");
	CHECK(has(file, "save four"), "the live file is untouched");

	CHECK(backupRotate("/no/such/file", dir, "Game.sav", 3)==-1, "a missing file fails");
	snprintf(p, sizeof(p), "%s/Game.sav.1", dir); CHECK(has(p, "save four"), "and moves nothing");
	CHECK(backupRotate(file, dir, "Game.sav", 0)==-1, "keep 0 is refused");
	// keep 1: just the latest
	char d1[700]; snprintf(d1, sizeof(d1), "%s/one", root);
	put(file, "a"); backupRotate(file, d1, "Game.sav", 1); put(file, "b"); backupRotate(file, d1, "Game.sav", 1);
	snprintf(p, sizeof(p), "%s/Game.sav.1", d1); CHECK(has(p, "b"), "keep 1 holds only the latest");
	snprintf(p, sizeof(p), "%s/Game.sav.2", d1); CHECK(!exists(p), "keep 1 makes no .2");
	// a folder that can't be made
	snprintf(p, sizeof(p), "%s/Game.sav/under-a-file", root);
	CHECK(backupRotate(file, p, "Game.sav", 3)==-1, "a folder that can't be created fails cleanly");
	return fails ? 1 : 0;
}
