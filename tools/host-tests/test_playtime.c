// play time records (Playtime_* in utils.c)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#define MAX_PATH 1024
#include "gen/playtime_types.inc"
#include "gen/writeFileAtomic.inc"
#include "gen/putFile.inc"
#include "gen/playtime.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static PlayRecord recs[16];
static int parse(const char* text) { char* b = strdup(text); int n = Playtime_parse(b, recs, 16); free(b); return n; }
int main(void) {
	int n = parse("3600\t4\t1700000000\t/roms/A.nes\n120\t1\t1700000500\t/roms/B (USA).gba\r\n");
	CHECK(n==2 && recs[0].seconds==3600 && recs[0].launches==4 && recs[0].last==1700000000 && !strcmp(recs[0].path, "/roms/A.nes"), "parses a line");
	CHECK(!strcmp(recs[1].path, "/roms/B (USA).gba") && recs[1].seconds==120, "CRLF line, spaces in the path");
	CHECK(parse("junk\n1\t2\n1\t2\t3\n\t\t\t\n-5\t1\t1\t/x\n")==1 && recs[0].seconds==0, "junk skipped, negative seconds become 0, empty path skipped");
	CHECK(parse("")==0, "empty");

	n = 0;
	n = Playtime_add(recs, n, 16, "/a", 60, 1, 1000);
	n = Playtime_add(recs, n, 16, "/b", 0, 1, 1010);
	n = Playtime_add(recs, n, 16, "/a", 30, 0, 1020);
	CHECK(n==2 && recs[0].seconds==90 && recs[0].launches==1 && recs[0].last==1020, "adds to an existing record");
	CHECK(recs[1].seconds==0 && recs[1].launches==1, "a new record");
	n = Playtime_add(recs, n, 16, "/a", 10, 0, 500);
	CHECK(recs[0].last==1020, "last played never goes backwards");
	CHECK(Playtime_add(recs, n, 16, "", 1, 1, 1)==n && Playtime_add(recs, n, 16, "/t\tab", 1, 1, 1)==n && Playtime_add(recs, n, 16, NULL, 1, 1, 1)==n, "bad paths ignored");
	CHECK(Playtime_add(recs, 2, 2, "/new", 1, 1, 1)==2, "full table: no new record");
	char out[512];
	int len = Playtime_format(recs, n, out, sizeof(out));
	CHECK(len==(int)strlen(out) && !strncmp(out, "100\t1\t1020\t/a\n", 14), "formats");
	char* copy = strdup(out); int m = Playtime_parse(copy, recs, 16); free(copy);
	CHECK(m==n, "round trip");
	char tiny[20]; Playtime_format(recs, n, tiny, sizeof(tiny)); CHECK(strlen(tiny)<sizeof(tiny), "small buffer respected");

	char name[128];
	Playtime_name("/storage/roms/Roms/GBA (MGBA)/Pokemon Heart and Soul (v1.2.1).gba", name, sizeof(name)); CHECK(!strcmp(name, "Pokemon Heart and Soul"), "name: folders, extension and (tags) removed");
	Playtime_name("/r/Mr. Do!.nes", name, sizeof(name)); CHECK(!strcmp(name, "Mr. Do!"), "name: dot inside the title stays");
	Playtime_name("/r/Mr. Do!", name, sizeof(name)); CHECK(!strcmp(name, "Mr. Do!"), "name: no extension");
	Playtime_name("/r/Game (USA) (Rev 1) [!].sfc", name, sizeof(name)); CHECK(!strcmp(name, "Game"), "name: several tags");
	Playtime_name("(Tag Only).gb", name, sizeof(name)); CHECK(!strcmp(name, "(Tag Only)"), "name: a title that is all tag is kept");
	Playtime_name("/r/Castlevania - Symphony of the Night.chd", name, sizeof(name)); CHECK(!strcmp(name, "Castlevania - Symphony of the Night"), "name: chd");

	char dur[32];
	Playtime_duration(0, dur, sizeof(dur)); CHECK(!strcmp(dur, "<1 min"), "duration: none");
	Playtime_duration(59, dur, sizeof(dur)); CHECK(!strcmp(dur, "<1 min"), "duration: 59 s");
	Playtime_duration(60, dur, sizeof(dur)); CHECK(!strcmp(dur, "1 min"), "duration: 1 min");
	Playtime_duration(3599, dur, sizeof(dur)); CHECK(!strcmp(dur, "59 min"), "duration: 59 min");
	Playtime_duration(3600+5*60, dur, sizeof(dur)); CHECK(!strcmp(dur, "1h 05m"), "duration: 1h 05m");
	Playtime_duration(100*3600+59*60+59, dur, sizeof(dur)); CHECK(!strcmp(dur, "100h 59m"), "duration: 100h");
	char dir[512], file[600];
	snprintf(dir, sizeof(dir), "%s/playtime-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
	mkdir(dir, 0777);
	snprintf(file, sizeof(file), "%s/playtime.txt", dir);
	Playtime_save(file, "/roms/Zelda.sfc", 0, 1, 2000);
	Playtime_save(file, "/roms/Zelda.sfc", 300, 0, 2300);
	Playtime_save(file, "/roms/Tetris.gb", 60, 1, 2400);
	FILE* f = fopen(file, "r"); char buf[1024] = ""; size_t k = f ? fread(buf, 1, sizeof(buf)-1, f) : 0; buf[k] = 0; if (f) fclose(f);
	CHECK(!strcmp(buf, "300\t1\t2300\t/roms/Zelda.sfc\n60\t1\t2400\t/roms/Tetris.gb\n"), "Playtime_save creates, updates and appends");
	return fails ? 1 : 0;
}
