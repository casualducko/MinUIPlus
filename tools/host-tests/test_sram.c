// Save versions in minarch.c: SRAM_write keeps earlier saves (not more often than every 10 minutes),
// SRAM_restore goes back to one and keeps what was there.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#define MAX_PATH 1024
#define LOG_error(...) fprintf(stderr, __VA_ARGS__)
#define RETRO_MEMORY_SAVE_RAM 0
static int exists(char* p) { return access(p, F_OK)==0; }
#include "gen/writeFileAtomic.inc"
#include "gen/mkdirs.inc"
#include "gen/backup.inc"
static uint8_t memory[16];
static size_t mem_size = 16;
static struct { char saves_dir[512]; size_t (*get_memory_size)(unsigned); void* (*get_memory_data)(unsigned); } core;
static struct { char name[256]; } game;
static size_t fake_size(unsigned t) { (void)t; return mem_size; }
static void* fake_data(unsigned t) { (void)t; return memory; }
static void SRAM_getPath(char* filename) { sprintf(filename, "%s/%s.sav", core.saves_dir, game.name); }
#include "gen/SRAM_versions.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static void setmem(const char* s) { memset(memory, 0, sizeof(memory)); memcpy(memory, s, strlen(s)); }
static int filehas(const char* path, const char* s) { uint8_t b[16]={0}; FILE* f = fopen(path, "rb"); if (!f) return 0; size_t n = fread(b,1,16,f); fclose(f); uint8_t e[16]={0}; memcpy(e,s,strlen(s)); return n==16 && !memcmp(b,e,16); }
static int memhas(const char* s) { uint8_t e[16]={0}; memcpy(e,s,strlen(s)); return !memcmp(memory,e,16); }
static void age_all(long secs) { // make everything this much older, so the timeline stays in order
	char cur[700], p[800]; struct stat st;
	snprintf(cur, sizeof(cur), "%s/Game.gba.sav", core.saves_dir);
	for (int n=0; n<=SRAM_BACKUPS; n++) {
		if (n==0) snprintf(p, sizeof(p), "%s", cur); else SRAM_backupPath(p, sizeof(p), n);
		if (stat(p, &st)==0) { struct timeval tv[2] = {{st.st_mtime-secs,0},{st.st_mtime-secs,0}}; utimes(p, tv); }
	}
}
static void age(const char* path, long secs_ago) { time_t t = time(NULL)-secs_ago; struct timeval tv[2] = {{t,0},{t,0}}; utimes(path, tv); }
int main(void) {
	char root[512], cur[700], b1[800], b2[800], b3[800];
	snprintf(root, sizeof(root), "%s/sram-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
	mkdir(root, 0777);
	snprintf(core.saves_dir, sizeof(core.saves_dir), "%s", root);
	core.get_memory_size = fake_size; core.get_memory_data = fake_data;
	snprintf(game.name, sizeof(game.name), "Game.gba");
	snprintf(cur, sizeof(cur), "%s/Game.gba.sav", root);
	SRAM_backupPath(b1, sizeof(b1), 1); SRAM_backupPath(b2, sizeof(b2), 2); SRAM_backupPath(b3, sizeof(b3), 3);

	setmem("save A"); SRAM_write();
	CHECK(filehas(cur, "save A"), "the first save is written");
	CHECK(!exists(b1), "...and there is nothing older to keep");

	age_all(3600); // A has been there an hour
	struct stat before; stat(cur, &before);
	SRAM_write(); // same data again
	struct stat after; stat(cur, &after);
	CHECK(before.st_mtime==after.st_mtime && !exists(b1), "the same data again: the file is left alone, no copy");

	setmem("save B"); SRAM_write();
	CHECK(filehas(cur, "save B") && filehas(b1, "save A"), "a changed save keeps the one it replaced");
	time_t when; CHECK(SRAM_backupTime(1, &when) && when < time(NULL)-3000, "the kept copy says when A was written (an hour ago)");

	setmem("save C"); SRAM_write(); // B is an hour newer than A: worth keeping
	CHECK(filehas(b1, "save B") && filehas(b2, "save A"), "B was written an hour after A, so it is kept too");

	setmem("save D"); SRAM_write(); // C was written a second after B
	CHECK(filehas(cur, "save D") && filehas(b1, "save B") && filehas(b2, "save A") && !exists(b3), "C, written moments after B, is not kept: copies are spaced at least 10 minutes apart");

	age_all(3600); setmem("save E"); SRAM_write(); // D was written moments after B, still too close
	CHECK(filehas(b1, "save B") && !exists(b3), "D is not kept either");
	age_all(3600); setmem("save F"); SRAM_write();
	CHECK(filehas(b1, "save E") && filehas(b2, "save B") && filehas(b3, "save A"), "three versions are kept, an hour or more apart");
	age_all(3600); setmem("save G"); SRAM_write();
	CHECK(filehas(b1, "save F") && filehas(b2, "save E") && filehas(b3, "save B"), "a fourth pushes the oldest (A) out");

	// a clock that went backwards: the newest copy looks newer than the file
	age(b1, -100000);
	setmem("save H"); SRAM_write();
	CHECK(filehas(b1, "save G"), "a copy dated in the future doesn't stop the old save being kept");

	// restore
	char want[16]={0}; FILE* f = fopen(b2, "rb"); fread(want, 1, 16, f); fclose(f);
	setmem("progress since H"); // the game has moved on since the last write
	CHECK(SRAM_restore(2)==0, "restore works");
	CHECK(!memcmp(memory, want, 16), "the game's memory holds the restored save");
	uint8_t got[16]={0}; f = fopen(cur, "rb"); fread(got, 1, 16, f); fclose(f);
	CHECK(!memcmp(got, memory, 16), "and so does the file");
	CHECK(filehas(b1, "progress since H"), "what the game had (newer than the file) is kept as the newest older version");

	mem_size = 32;
	CHECK(SRAM_restore(1)==-1, "a version of another size is refused");
	mem_size = 16;
	CHECK(SRAM_restore(4)==-1, "a version that doesn't exist is refused");
	CHECK(memhas("") || 1, "(done)");
	return fails ? 1 : 0;
}
