// The Update tool's parsing, with no graphics and no network (see update.c).
#ifndef UPDATE_CORE_H
#define UPDATE_CORE_H
#include <stdint.h>
#include <stddef.h>

#define UPDATE_DEFAULT_REPO "casualducko/MinUIPlus"

typedef struct {
	char tag[64];          // "v2026.10.25.1"
	char pkg_name[160];    // the .tar.gz asset
	char pkg_url[400];     // its API url (what to download)
	uint64_t pkg_size;     // bytes, 0 if the release didn't say
	char sum_url[400];     // the .sha256 asset that goes with it
} UpdateRelease;

// update.cfg: repo=owner/name and token=... (optional, for a private repo). Lines starting with # are ignored.
void Update_parseConfig(const char* text, char* repo, size_t repo_size, char* token, size_t token_size);
// A GitHub "latest release" answer. 1 if it has a tag, a .tar.gz package and its .sha256.
int Update_parseRelease(const char* json, UpdateRelease* out);
// Compares two version tags by their numbers (digit runs, left to right): -1, 0 or 1. "v2026.10.25.2" > "v2026.10.25.1".
int Update_compareVersions(const char* a, const char* b);
// The 64 hex digits at the start of a line like sha256sum prints ("<hash>  file"), lower-cased into out[65]. 1 if valid.
int Update_parseSum(const char* text, char* out);
// 1 if every path in a `tar tzf` listing stays inside the folder it is unpacked into (no absolute paths, no "..").
int Update_pathsSafe(const char* listing);
// "https://" and a plausible repo only: 1 if repo looks like owner/name with safe characters.
int Update_repoValid(const char* repo);

#endif
