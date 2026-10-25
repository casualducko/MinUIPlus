// collections (Collection_* in utils.c): the text files MinUI reads from /Collections
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#define MAX_PATH 1024
#include "gen/collections_types.inc"
#include "gen/writeFileAtomic.inc"
#include "gen/putFile.inc"
#include "gen/getFile.inc"
#include "gen/allocFile.inc"
#include "gen/mkdirs.inc"
#include "gen/collections.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
int main(void) {
	char out[256];
	const char* g = "/Roms/GBC (GBC)/Game.gbc";
	CHECK(!Collection_textHas("", g), "an empty collection has nothing");
	CHECK(Collection_textHas("/Roms/A.nes\n/Roms/GBC (GBC)/Game.gbc\n", g), "finds a line");
	CHECK(Collection_textHas("/Roms/A.nes\r\n/Roms/GBC (GBC)/Game.gbc\r\n", g), "windows line endings");
	CHECK(Collection_textHas("/Roms/GBC (GBC)/Game.gbc", g), "last line without a newline");
	CHECK(!Collection_textHas("/Roms/GBC (GBC)/Game.gbc.bak\n", g), "a longer path is not a match");
	CHECK(!Collection_textHas("/Roms/GBC (GBC)/Game.gb\n", g), "a shorter path is not a match");
	int n = Collection_textAdd("", g, out, sizeof(out));
	CHECK(n>0 && !strcmp(out, "/Roms/GBC (GBC)/Game.gbc\n"), "add to nothing");
	n = Collection_textAdd("/Roms/A.nes", g, out, sizeof(out));
	CHECK(n>0 && !strcmp(out, "/Roms/A.nes\n/Roms/GBC (GBC)/Game.gbc\n"), "add after a line with no newline");
	n = Collection_textAdd("/Roms/A.nes\n/Roms/GBC (GBC)/Game.gbc\n", g, out, sizeof(out));
	CHECK(n>0 && !strcmp(out, "/Roms/A.nes\n/Roms/GBC (GBC)/Game.gbc\n"), "adding twice changes nothing");
	CHECK(Collection_textAdd("x\n", g, out, 10)==-1, "no room: -1");
	n = Collection_textRemove("/Roms/A.nes\n/Roms/GBC (GBC)/Game.gbc\n/Roms/B.nes\n", g, out, sizeof(out));
	CHECK(n>0 && !strcmp(out, "/Roms/A.nes\n/Roms/B.nes\n"), "remove the middle line");
	n = Collection_textRemove("/Roms/GBC (GBC)/Game.gbc\r\n/Roms/GBC (GBC)/Game.gbc\n", g, out, sizeof(out));
	CHECK(n==0 && !out[0], "every copy removed, \\r\\n too");
	n = Collection_textRemove("/Roms/A.nes\n", g, out, sizeof(out));
	CHECK(n>0 && !strcmp(out, "/Roms/A.nes\n"), "removing what isn't there changes nothing");
	CHECK(!strcmp(Collection_rel("/mnt/SDCARD/Roms/A.nes", "/mnt/SDCARD"), "/Roms/A.nes"), "path below the card");
	CHECK(!Collection_rel("/mnt/SDCARD2/Roms/A.nes", "/mnt/SDCARD") && !Collection_rel("/other/A.nes", "/mnt/SDCARD"), "not below the card");

	char dir[256];
	snprintf(dir, sizeof(dir), "/tmp/coll-test-%d", (int)getpid());
	char cmd[300];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
	system(cmd);
	char file[300];
	snprintf(file, sizeof(file), "%s/Collections/Favorites.txt", dir);
	CHECK(!Collection_contains(file, g), "no file: not in it");
	CHECK(Collection_set(file, g, 1)==0 && Collection_contains(file, g), "adding makes the folder and the file");
	CHECK(Collection_set(file, "/Roms/A.nes", 1)==0 && Collection_contains(file, g) && Collection_contains(file, "/Roms/A.nes"), "a second game");
	CHECK(Collection_set(file, g, 0)==0 && !Collection_contains(file, g) && Collection_contains(file, "/Roms/A.nes"), "removing one keeps the other");
	char* kept = allocFile(file);
	CHECK(kept && !strcmp(kept, "/Roms/A.nes\n"), "file content");
	free(kept);
	char names[COLLECTION_MAX][COLLECTION_NAME_MAX];
	char other[300];
	snprintf(other, sizeof(other), "%s/Collections/mario.txt", dir); putFile(other, "x\n");
	snprintf(other, sizeof(other), "%s/Collections/map.txt", dir); putFile(other, "x\n");
	snprintf(other, sizeof(other), "%s/Collections/.hidden.txt", dir); putFile(other, "x\n");
	snprintf(other, sizeof(other), "%s/Collections/notes.md", dir); putFile(other, "x\n");
	snprintf(other, sizeof(other), "%s/Collections", dir);
	int c = Collection_list(other, names, COLLECTION_MAX);
	CHECK(c==2 && !strcmp(names[0], "Favorites") && !strcmp(names[1], "mario"), "lists the collections, sorted, without map.txt, hidden files or other types");
	CHECK(Collection_list("/tmp/does-not-exist-xyz", names, COLLECTION_MAX)==0, "no folder: none");
	system(cmd);
	printf(fails ? "\n%d FAILED\n" : "\nall collections tests passed\n", fails);
	return fails ? 1 : 0;
}
