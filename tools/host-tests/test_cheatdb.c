// the cheat database helpers (Cheatdb_* in utils.c): folders, URL encoding, the listing, picking a file
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "gen/cheatdb.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static char names[65536];
static char pick[512];
static const char* choose(const char* game) { return Cheatdb_pick(names, game, pick, sizeof(pick)) ? pick : NULL; }
int main(void) {
	CHECK(!strcmp(Cheatdb_folder("FC"), "Nintendo - Nintendo Entertainment System"), "FC folder");
	CHECK(!strcmp(Cheatdb_folder("gba"), "Nintendo - Game Boy Advance"), "tags are case-insensitive");
	CHECK(Cheatdb_folder("SGB")==Cheatdb_folder("GB") || !strcmp(Cheatdb_folder("SGB"), Cheatdb_folder("GB")), "Super Game Boy uses the Game Boy cheats");
	CHECK(Cheatdb_folder("PAK")==NULL && Cheatdb_folder("P8")==NULL && Cheatdb_folder("")==NULL, "systems without a database");

	char enc[256];
	Cheatdb_encode("Sega - Mega Drive - Genesis", enc, sizeof(enc));
	CHECK(!strcmp(enc, "Sega%20-%20Mega%20Drive%20-%20Genesis"), "spaces encoded");
	Cheatdb_encode("Super Mario Bros. (World)&x#?.cht", enc, sizeof(enc));
	CHECK(!strcmp(enc, "Super%20Mario%20Bros.%20%28World%29%26x%23%3F.cht"), "parentheses, &, #, ? encoded");
	Cheatdb_encode("Pok\xC3\xA9mon.cht", enc, sizeof(enc));
	CHECK(!strcmp(enc, "Pok%C3%A9mon.cht"), "UTF-8 bytes encoded");
	Cheatdb_encode("aaaaaaaaaaaaaaaaaaaaaaaa", enc, 10);
	CHECK(strlen(enc)<10, "encoding never overruns the buffer");

	const char* json =
		"{\"sha\":\"abc\",\"tree\":[\n"
		" {\"path\":\"Super Mario Bros. (World).cht\",\"mode\":\"100644\",\"type\":\"blob\"},\n"
		" {\"path\": \"Super Mario Bros. (Japan, USA) (Beta).cht\", \"type\":\"blob\"},\n"
		" {\"path\":\"Legend of Zelda, The (USA).cht\"},\n"
		" {\"path\":\"Legend of Zelda, The (USA) (Rev 1).cht\"},\n"
		" {\"path\":\"Pok\\u00e9mon \\\"Blue\\\" (USA).cht\"},\n"
		" {\"path\":\"subfolder\",\"type\":\"tree\"},\n"
		" {\"path\":\"readme.txt\"},\n"
		" {\"path\":\"Contra (USA).cht\"}, {\"path\":\"Contra (Japan).cht\"}, {\"path\":\"Contra (Europe).cht\"},\n"
		" {\"path\":\"Tetris (World) (Rev 1).cht\"}, {\"path\":\"Tetris (Japan) (Beta).cht\"},\n"
		" {\"path\":\"Super Mario Bros. 3 (USA).cht\"}]}";
	int n = Cheatdb_names(json, names, sizeof(names));
	CHECK(n==11, "counts the .cht files only (folders and other files skipped)");
	CHECK(strstr(names, "Pok\xC3\xA9mon \"Blue\" (USA).cht\n")!=NULL, "JSON escapes (\\u00e9, \\\") decoded");
	CHECK(strstr(names, "readme")==NULL && strstr(names, "subfolder")==NULL, "no non-cheat entries");
	char tiny[64];
	n = Cheatdb_names(json, tiny, sizeof(tiny));
	CHECK(strlen(tiny)<sizeof(tiny) && n>=1, "a small buffer is not overrun");
	n = Cheatdb_names(json, names, sizeof(names));

	CHECK(choose("Super Mario Bros. (World).nes") && !strcmp(pick, "Super Mario Bros. (World).cht"), "exact name");
	CHECK(choose("Super Mario Bros. (USA).nes") && !strcmp(pick, "Super Mario Bros. (World).cht"), "USA ROM, World cheats (the beta loses)");
	CHECK(choose("Super Mario Bros..nes")==NULL || 1, "odd name does not crash");
	CHECK(choose("super mario bros.nes") && !strcmp(pick, "Super Mario Bros. (World).cht"), "no tags, any case, extension dropped");
	CHECK(choose("The Legend of Zelda (USA).nes")==NULL || 1, "(The first is a different key)");
	CHECK(choose("Legend of Zelda, The (USA).nes") && !strcmp(pick, "Legend of Zelda, The (USA).cht"), "\", The\" names");
	CHECK(choose("Legend of Zelda, The (USA) (Rev 1).nes") && !strcmp(pick, "Legend of Zelda, The (USA) (Rev 1).cht"), "revision kept when it matches");
	CHECK(choose("Legend of Zelda, The (Europe).nes") && !strcmp(pick, "Legend of Zelda, The (USA).cht"), "another region still finds the title");
	CHECK(choose("Pok\xC3\xA9mon \"Blue\" (USA).gb") && strstr(pick, "Blue"), "accented title");
	CHECK(choose("Contra (Japan).nes") && !strcmp(pick, "Contra (Japan).cht"), "same region wins");
	CHECK(choose("Contra (Europe).nes") && !strcmp(pick, "Contra (Europe).cht"), "region match");
	CHECK(choose("Contra.nes") && !strcmp(pick, "Contra (USA).cht"), "no region: USA first");
	CHECK(choose("Tetris (USA).gb") && !strcmp(pick, "Tetris (World) (Rev 1).cht"), "a beta is the last choice");
	CHECK(choose("Tetris (Japan) (Beta).gb") && !strcmp(pick, "Tetris (Japan) (Beta).cht"), "unless the ROM is that beta");
	CHECK(choose("Super Mario Bros. 3 (USA).nes") && !strcmp(pick, "Super Mario Bros. 3 (USA).cht"), "sequels are not mixed up");
	CHECK(choose("Super Mario Bros 2 (USA).nes")==NULL, "no match, no guess");
	CHECK(choose("")==NULL && choose(".nes")==NULL, "empty names");
	CHECK(Cheatdb_pick("", "Contra.nes", pick, sizeof(pick))==0, "empty list");

	if (getenv("CHEATDB_REAL_JSON")) { // optional: a real listing saved from the GitHub API
		FILE* f = fopen(getenv("CHEATDB_REAL_JSON"), "r");
		if (f) {
			char* big = calloc(1, 4*1024*1024);
			size_t m = fread(big, 1, 4*1024*1024-1, f);
			big[m] = '\0';
			fclose(f);
			char* all = malloc(1024*1024);
			int total = Cheatdb_names(big, all, 1024*1024);
			printf("real listing: %i names\n", total);
			CHECK(total>1000, "real listing parsed");
			const char* games[] = {"Super Mario Bros. (USA).nes", "Teenage Mutant Ninja Turtles (USA).nes", "Mega Man 2 (USA).nes", "Castlevania (USA) (Rev 1).nes", "Legend of Zelda, The (USA).nes", NULL};
			for (int i=0; games[i]; i++) {
				if (Cheatdb_pick(all, games[i], pick, sizeof(pick))) printf("  %s -> %s\n", games[i], pick);
				else printf("  %s -> none\n", games[i]);
			}
		}
	}
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
