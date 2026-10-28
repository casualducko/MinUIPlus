// Client side of the romulus API (see ROMULUS_HANDOFF.md): config, URLs, a curl job that runs
// in the background, parsing the answers, and deciding where a ROM goes on the card.
// No SDL in here so it can be tested on a computer.
#ifndef ROMULUS_RAPI_H
#define ROMULUS_RAPI_H
#include <stddef.h>
#include <sys/types.h>

#define RCFG_MAX_MAPS 16

typedef struct {
	char url[256];			// https://host, no trailing slash
	char token[256];
	int insecure;			// insecure_http=1 in the config: allow http:// (for testing only)
	int map_count;
	struct { char slug[48]; char folder[96]; } map[RCFG_MAX_MAPS]; // map=slug:Folder (TAG)
} RConfig;

enum { RCFG_OK=0, RCFG_MISSING=-1, RCFG_INCOMPLETE=-2, RCFG_NEEDS_HTTPS=-3 };
int rcfg_load(const char* path, RConfig* cfg);
const char* rcfg_message(int code);
int rcfg_write_template(const char* path); // only if the file doesn't exist yet

// systems we know how to file
typedef struct {
	const char* slugs[8];	// romulus platform slugs that mean this system
	const char* name;		// folder name when we have to make one
	const char* label;		// short, for the list
	const char* tags[3];	// folder tags in order of preference, eg. GBA is "MGBA" on some cards
	const char* exts[10];	// file extensions the emulator takes (lowercase, no dot)
} SysDef;
const SysDef* sys_for_slug(const char* slug);

typedef int (*RHasEmu)(const char* tag, void* ctx);
// Fill dir with the ROM folder for this system under roms_root (existing "Name (TAG)" folder
// first, otherwise "Name (TAG)" for the first tag that has an emulator, created if create).
// Returns 0, or -1 if no emulator for it / the folder can't be made.
int rom_dest_dir(const RConfig* cfg, const char* slug, const SysDef* sys, const char* roms_root, RHasEmu has_emu, void* ctx, int create, char* dir, size_t cap);
// file name for a download: base name only, nothing a FAT card or the shell dislikes
void rom_safe_name(const char* in, const SysDef* sys, char* out, size_t cap);

// request paths (appended to cfg->url)
void rapi_urlencode(const char* in, char* out, size_t cap);
void rapi_platforms_path(char* out, size_t cap);
void rapi_roms_path(char* out, size_t cap, const char* platform, const char* platforms_csv, const char* query, int offset, int limit);
void rapi_download_path(char* out, size_t cap, long long id);

// a curl run in the background
typedef struct {
	pid_t pid;
	int done;
	int curl_exit;		// valid once done
	int http;			// valid once done, 0 if curl never got an answer
	char out_path[512];
	char code_path[512];
	char err_path[512];
	char hdr_path[512];
} RJob;
// download=1: no total time limit, only a stall limit. Returns 0 or -1.
int rjob_start(RJob* job, const RConfig* cfg, const char* api_path, const char* out_path, int download);
int rjob_poll(RJob* job); // 1 once finished
void rjob_cancel(RJob* job);
long rjob_bytes(const RJob* job); // size of the output so far
const char* rjob_error(const RJob* job); // NULL when it worked (HTTP 200)
void rjob_cleanup(RJob* job); // removes the little files, not out_path

typedef struct { char slug[48]; int count; } RPlat;
typedef struct {
	long long id;
	char name[200], platform[48], region[32], file_name[200];
	long long size;
} RRom;
typedef struct { RRom* items; int count, cap; } RRomList;
int rapi_parse_platforms(const char* body, size_t len, RPlat* out, int max); // count or -1
int rapi_parse_roms(const char* body, size_t len, RRomList* list, long* total, int* raw); // number appended or -1; raw = entries the server sent (to page by)
void rromlist_free(RRomList* list);
void rromlist_sort_newest(RRomList* list); // highest id first (the server numbers games in the order it got them; it has no date or sort of its own)

// the ids of games already downloaded (so the list can say so even for zips)
int ledger_has(const char* path, long long id);
void ledger_add(const char* path, long long id);
#endif
