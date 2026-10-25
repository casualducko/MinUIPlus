#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <time.h>

int prefixMatch(char* pre, char* str);
int suffixMatch(char* suf, char* str);
int exactMatch(char* str1, char* str2);
int containsString(char* haystack, char* needle);
int hide(char* file_name);

void getDisplayName(const char* in_name, char* out_name);
void getEmuName(const char* in_name, char* out_name);
void getEmuPath(char* emu_name, char* pak_path);

void normalizeNewline(char* line);
void trimTrailingNewlines(char* line);
void trimSortingMeta(char** str);

int exists(char* path);
// A random game: played[i] is 1 for a game that has been played; the pick is among those that have not
// (any, if all have). state is the generator's (xorshift32; start it from the clock, never 0).
int Random_pick(const unsigned char* played, int count, uint32_t* state); // an index, or -1 when count<=0
// An HTTP Date header (RFC 7231: "Date: Sat, 25 Oct 2026 04:30:00 GMT") in the text of a response's headers
time_t HTTPDate_parse(const char* headers); // seconds since 1970 (UTC), or 0 if there is no usable date (before 2024 or after 2100 counts as none)
// The recently played list (recent.txt: paths relative to the card, newest first, an optional \talias after each)
size_t Recents_bump(const char* text, const char* rel_path, int max, char* out, size_t size); // the list with rel_path at the top (its alias kept), at most max lines; returns the length
void Recents_add(const char* sd_path); // puts a game on top of the launcher's list (RECENT_PATH)
int writeFileAtomic(const char* path, const void* data, size_t size); // writes path.tmp, syncs, renames over path: the old file survives any failure; 0 or -1
int mkdirs(const char* path); // mkdir -p without a shell (a folder name is never a command); 0 if it exists afterwards, -1 if not
int fileEquals(const char* path, const void* data, size_t size); // 1 if the file holds exactly these bytes
// play time, kept in one text file: lines of "seconds<TAB>launches<TAB>last played (unix time)<TAB>ROM path"
#define PLAYTIME_MAX 4096
typedef struct PlayRecord {
	char path[512];
	long seconds;
	int launches;
	long last;
} PlayRecord;
int Playtime_parse(char* text, PlayRecord* out, int max); // edits text; returns the number of records
int Playtime_add(PlayRecord* records, int count, int max, const char* path, long seconds, int launches, long now); // new count
int Playtime_format(const PlayRecord* records, int count, char* out, int size); // returns the length
void Playtime_name(const char* path, char* out, int size); // a title for the list: no folders, no extension, no (tags)
void Playtime_duration(long seconds, char* out, int size); // "<1 min", "42 min", "3h 05m"
void Playtime_save(const char* file, const char* path, long seconds, int launches, long now); // read, add, write
// Cheat database: the libretro-database "cht" folders on GitHub (what RetroArch downloads from)
const char* Cheatdb_folder(const char* tag); // the folder for a system tag, or NULL if the database has none
void Cheatdb_encode(const char* in, char* out, int size); // percent-encodes for a URL
int Cheatdb_names(const char* json, char* out, int size); // the "x.cht" names in a GitHub tree listing, one per line; returns how many
int Cheatdb_pick(const char* names, const char* game, char* out, int size); // best file for a ROM name; 1 if found
int backupRotate(const char* file, const char* dir, const char* base, int keep); // file -> dir/base.1, .1 -> .2 ... (keeps the file's mtime); 0 or -1
void touch(char* path);
void putFile(char* path, char* contents);
char* allocFile(char* path); // caller must free
void getFile(char* path, char* buffer, size_t buffer_size);
void putInt(char* path, int value);
int getInt(char* path);

uint64_t getMicroseconds(void);

#endif
