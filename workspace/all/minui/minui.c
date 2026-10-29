#include <stdio.h>
#include <stdlib.h>
#include <msettings.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "info_view.h"

///////////////////////////////////////

typedef struct Array {
	int count;
	int capacity;
	void** items;
} Array;

static Array* Array_new(void) {
	Array* self = malloc(sizeof(Array));
	self->count = 0;
	self->capacity = 8;
	self->items = malloc(sizeof(void*) * self->capacity);
	return self;
}
static void Array_push(Array* self, void* item) {
	if (self->count>=self->capacity) {
		self->capacity *= 2;
		self->items = realloc(self->items, sizeof(void*) * self->capacity);
	}
	self->items[self->count++] = item;
}
static void Array_unshift(Array* self, void* item) {
	if (self->count==0) return Array_push(self, item);
	Array_push(self, NULL); // ensures we have enough capacity
	for (int i=self->count-2; i>=0; i--) {
		self->items[i+1] = self->items[i];
	}
	self->items[0] = item;
}
static void* Array_pop(Array* self) {
	if (self->count==0) return NULL;
	return self->items[--self->count];
}
static void Array_free(Array* self) {
	free(self->items); 
	free(self);
}

static int StringArray_indexOf(Array* self, char* str) {
	for (int i=0; i<self->count; i++) {
		if (exactMatch(self->items[i], str)) return i;
	}
	return -1;
}
static void StringArray_free(Array* self) {
	for (int i=0; i<self->count; i++) {
		free(self->items[i]);
	}
	Array_free(self);
}

///////////////////////////////////////

typedef struct Hash {
	Array* keys;
	Array* values;
} Hash; // not really a hash

static Hash* Hash_new(void) {
	Hash* self = malloc(sizeof(Hash));
	self->keys = Array_new();
	self->values = Array_new();
	return self;
}
static void Hash_free(Hash* self) {
	StringArray_free(self->keys);
	StringArray_free(self->values);
	free(self);
}
static void Hash_set(Hash* self, char* key, char* value) {
	Array_push(self->keys, strdup(key));
	Array_push(self->values, strdup(value));
}
static char* Hash_get(Hash* self, char* key) {
	int i = StringArray_indexOf(self->keys, key);
	if (i==-1) return NULL;
	return self->values->items[i];
}

///////////////////////////////////////

enum EntryType {
	ENTRY_DIR,
	ENTRY_PAK,
	ENTRY_ROM,
};
typedef struct Entry {
	char* path;
	char* name;
	char* unique;
	int type;
	int alpha; // index in parent Directory's alphas Array, which points to the index of an Entry in its entries Array :sweat_smile:
} Entry;

static Entry* Entry_new(char* path, int type) {
	char display_name[MAX_PATH];
	getDisplayName(path, display_name);
	Entry* self = malloc(sizeof(Entry));
	self->path = strdup(path);
	self->name = strdup(display_name);
	self->unique = NULL;
	self->type = type;
	self->alpha = 0;
	return self;
}
static void Entry_free(Entry* self) {
	free(self->path);
	free(self->name);
	if (self->unique) free(self->unique);
	free(self);
}

static int EntryArray_sortEntry(const void* a, const void* b) {
	Entry* item1 = *(Entry**)a;
	Entry* item2 = *(Entry**)b;
	return strcasecmp(item1->name, item2->name);
}
static void EntryArray_sort(Array* self) {
	qsort(self->items, self->count, sizeof(void*), EntryArray_sortEntry);
}

static void EntryArray_free(Array* self) {
	for (int i=0; i<self->count; i++) {
		Entry_free(self->items[i]);
	}
	Array_free(self);
}

///////////////////////////////////////

#define INT_ARRAY_MAX 27
typedef struct IntArray {
	int count;
	int items[INT_ARRAY_MAX];
} IntArray;
static IntArray* IntArray_new(void) {
	IntArray* self = malloc(sizeof(IntArray));
	self->count = 0;
	memset(self->items, 0, sizeof(int) * INT_ARRAY_MAX);
	return self;
}
static void IntArray_push(IntArray* self, int i) {
	self->items[self->count++] = i;
}
static void IntArray_free(IntArray* self) {
	free(self);
}

///////////////////////////////////////

typedef struct Directory {
	char* path;
	char* name;
	Array* entries;
	IntArray* alphas;
	// rendering
	int selected;
	int start;
	int end;
} Directory;

static int getIndexChar(char* str) {
	char i = 0;
	char c = tolower(str[0]);
	if (c>='a' && c<='z') i = (c-'a')+1;
	return i;
}

static void getUniqueName(Entry* entry, char* out_name) { // NOTE: out_name needs to be MAX_PATH bytes
	char emu_tag[MAX_PATH];
	getEmuName(entry->path, emu_tag);
	snprintf(out_name, MAX_PATH, "%s (%s)", entry->name, emu_tag);
}

static void Directory_index(Directory* self) {
	int is_collection = prefixMatch(COLLECTIONS_PATH, self->path);
	int skip_index = exactMatch(FAUX_RECENT_PATH, self->path) || is_collection; // not alphabetized
	
	Hash* map = NULL;
	char map_path[MAX_PATH];
	sprintf(map_path, "%s/map.txt", is_collection ? COLLECTIONS_PATH : self->path);
	if (exists(map_path)) {
		FILE* file = fopen(map_path, "r");
		if (file) {
			map = Hash_new();
			char line[MAX_PATH];
			while (fgets(line,MAX_PATH,file)!=NULL) {
				normalizeNewline(line);
				trimTrailingNewlines(line);
				if (strlen(line)==0) continue; // skip empty lines

				char* tmp = strchr(line,'\t');
				if (tmp) {
					tmp[0] = '\0';
					char* key = line;
					char* value = tmp+1;
					Hash_set(map, key, value);
				}
			}
			fclose(file);
			
			int resort = 0;
			int filter = 0;
			for (int i=0; i<self->entries->count; i++) {
				Entry* entry = self->entries->items[i];
				char* filename = strrchr(entry->path, '/')+1;
				char* alias = Hash_get(map, filename);
				if (alias) {
					free(entry->name);
					entry->name = strdup(alias);
					resort = 1;
					if (!filter && hide(entry->name)) filter = 1;
				}
			}
			
			if (filter) {
				Array* entries = Array_new();
				for (int i=0; i<self->entries->count; i++) {
					Entry* entry = self->entries->items[i];
					if (hide(entry->name)) {
						Entry_free(entry);
					}
					else {
						Array_push(entries, entry);
					}
				}
				Array_free(self->entries); // not EntryArray_free because we've just moved the entries from the original to the filtered one!
				self->entries = entries;
			}
			if (resort) EntryArray_sort(self->entries);
		}
	}
	
	Entry* prior = NULL;
	int alpha = -1;
	int index = 0;
	for (int i=0; i<self->entries->count; i++) {
		Entry* entry = self->entries->items[i];
		if (map) {
			char* filename = strrchr(entry->path, '/')+1;
			char* alias = Hash_get(map, filename);
			if (alias) {
				free(entry->name);
				entry->name = strdup(alias);
			}
		}
		
		if (prior!=NULL && exactMatch(prior->name, entry->name)) {
			if (prior->unique) free(prior->unique);
			if (entry->unique) free(entry->unique);
			
			char* prior_filename = strrchr(prior->path, '/')+1;
			char* entry_filename = strrchr(entry->path, '/')+1;
			if (exactMatch(prior_filename, entry_filename)) {
				char prior_unique[MAX_PATH];
				char entry_unique[MAX_PATH];
				getUniqueName(prior, prior_unique);
				getUniqueName(entry, entry_unique);
				
				prior->unique = strdup(prior_unique);
				entry->unique = strdup(entry_unique);
			}
			else {
				prior->unique = strdup(prior_filename);
				entry->unique = strdup(entry_filename);
			}
		}

		if (!skip_index) {
			int a = getIndexChar(entry->name);
			if (a!=alpha) {
				index = self->alphas->count;
				IntArray_push(self->alphas, i);
				alpha = a;
			}
			entry->alpha = index;
		}
		
		prior = entry;
	}
	
	if (map) Hash_free(map);
}

static Array* getRoot(void);
static Array* getRecents(void);
static Array* getCollection(char* path);
static Array* getDiscs(char* path);
static Array* getEntries(char* path);

static Directory* Directory_new(char* path, int selected) {
	char display_name[MAX_PATH];
	getDisplayName(path, display_name);
	
	Directory* self = malloc(sizeof(Directory));
	self->path = strdup(path);
	self->name = strdup(display_name);
	if (exactMatch(path, SDCARD_PATH)) {
		self->entries = getRoot();
	}
	else if (exactMatch(path, FAUX_RECENT_PATH)) {
		self->entries = getRecents();
	}
	else if (!exactMatch(path, COLLECTIONS_PATH) && prefixMatch(COLLECTIONS_PATH, path) && suffixMatch(".txt", path)) {
		self->entries = getCollection(path);
	}
	else if (suffixMatch(".m3u", path)) {
		self->entries = getDiscs(path);
	}
	else {
		self->entries = getEntries(path);
	}
	self->alphas = IntArray_new();
	self->selected = selected;
	Directory_index(self);
	return self;
}
static void Directory_free(Directory* self) {
	free(self->path);
	free(self->name);
	EntryArray_free(self->entries);
	IntArray_free(self->alphas);
	free(self);
}

static void DirectoryArray_pop(Array* self) {
	Directory_free(Array_pop(self));
}
static void DirectoryArray_free(Array* self) {
	for (int i=0; i<self->count; i++) {
		Directory_free(self->items[i]);
	}
	Array_free(self);
}

///////////////////////////////////////

typedef struct Recent {
	char* path; // NOTE: this is without the SDCARD_PATH prefix!
	char* alias;
	int available;
} Recent;
 // yiiikes
static char* recent_alias = NULL;

static int hasEmu(char* emu_name);
static Recent* Recent_new(char* path, char* alias) {
	Recent* self = malloc(sizeof(Recent));

	char sd_path[MAX_PATH]; // only need to get emu name
	snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, path); // path might come from a file, a too long one just won't exist

	char emu_name[MAX_PATH];
	getEmuName(sd_path, emu_name);
	
	self->path = strdup(path);
	self->alias = alias ? strdup(alias) : NULL;
	self->available = hasEmu(emu_name);
	return self;
}
static void Recent_free(Recent* self) {
	free(self->path);
	if (self->alias) free(self->alias);
	free(self);
}

static int RecentArray_indexOf(Array* self, char* str) {
	for (int i=0; i<self->count; i++) {
		Recent* item = self->items[i];
		if (exactMatch(item->path, str)) return i;
	}
	return -1;
}
static void RecentArray_free(Array* self) {
	for (int i=0; i<self->count; i++) {
		Recent_free(self->items[i]);
	}
	Array_free(self);
}

///////////////////////////////////////

static Directory* top;
static Array* stack; // DirectoryArray
static Array* recents; // RecentArray

static int quit = 0;
static int can_resume = 0;
static int should_resume = 0; // set to 1 on BTN_RESUME but only if can_resume==1
static int simple_mode = 0;
static char slot_path[MAX_PATH];

static int restore_depth = -1;
static int restore_relative = -1;
static int restore_selected = 0;
static int restore_start = 0;
static int restore_end = 0;

///////////////////////////////////////

#define MAX_RECENTS 24 // a multiple of all menu rows
static void saveRecents(void) {
	// built in memory and put in place in one step, so a power cut can't leave an empty list
	size_t cap = 1;
	for (int i=0; i<recents->count; i++) {
		Recent* recent = recents->items[i];
		cap += strlen(recent->path) + (recent->alias ? strlen(recent->alias) + 1 : 0) + 1;
	}
	char* text = malloc(cap);
	if (!text) return;
	size_t n = 0;
	for (int i=0; i<recents->count; i++) {
		Recent* recent = recents->items[i];
		n += snprintf(text+n, cap-n, "%s", recent->path);
		if (recent->alias) n += snprintf(text+n, cap-n, "\t%s", recent->alias);
		n += snprintf(text+n, cap-n, "\n");
	}
	writeFileAtomic(RECENT_PATH, text, n);
	free(text);
}
static void addRecent(char* path, char* alias) {
	path += strlen(SDCARD_PATH); // makes paths platform agnostic
	int id = RecentArray_indexOf(recents, path);
	if (id==-1) { // add
		while (recents->count>=MAX_RECENTS) {
			Recent_free(Array_pop(recents));
		}
		Array_unshift(recents, Recent_new(path, alias));
	}
	else if (id>0) { // bump to top
		for (int i=id; i>0; i--) {
			void* tmp = recents->items[i-1];
			recents->items[i-1] = recents->items[i];
			recents->items[i] = tmp;
		}
	}
	saveRecents();
}

static int hasEmu(char* emu_name) {
	char pak_path[MAX_PATH];
	sprintf(pak_path, "%s/Emus/%s.pak/launch.sh", PAKS_PATH, emu_name);
	if (exists(pak_path)) return 1;

	sprintf(pak_path, "%s/Emus/%s/%s.pak/launch.sh", SDCARD_PATH, PLATFORM, emu_name);
	return exists(pak_path);
}
static int hasCue(char* dir_path, char* cue_path) { // NOTE: dir_path not rom_path
	char* tmp = strrchr(dir_path, '/') + 1; // folder name
	sprintf(cue_path, "%s/%s.cue", dir_path, tmp);
	return exists(cue_path);
}
static int hasM3u(char* rom_path, char* m3u_path) { // NOTE: rom_path not dir_path
	char* tmp;
	
	strcpy(m3u_path, rom_path);
	tmp = strrchr(m3u_path, '/') + 1;
	tmp[0] = '\0';
	
	// path to parent directory
	char base_path[MAX_PATH];
	strcpy(base_path, m3u_path);
	
	tmp = strrchr(m3u_path, '/');
	tmp[0] = '\0';
	
	// get parent directory name
	char dir_name[MAX_PATH];
	tmp = strrchr(m3u_path, '/');
	strcpy(dir_name, tmp);
	
	// dir_name is also our m3u file name
	tmp = m3u_path + strlen(m3u_path); 
	strcpy(tmp, dir_name);

	// add extension
	tmp = m3u_path + strlen(m3u_path);
	strcpy(tmp, ".m3u");
	
	return exists(m3u_path);
}

static int hasRecents(void) {
	LOG_info("hasRecents %s\n", RECENT_PATH);
	int has = 0;
	while (recents->count) Recent_free(Array_pop(recents)); // read again from the file: a second call (the root is rebuilt after a collection changes) must not add every game a second time
	
	Array* parent_paths = Array_new();
	if (exists(CHANGE_DISC_PATH)) {
		char sd_path[MAX_PATH];
		getFile(CHANGE_DISC_PATH, sd_path, MAX_PATH);
		if (exists(sd_path)) {
			char* disc_path = sd_path + strlen(SDCARD_PATH); // makes path platform agnostic
			Recent* recent = Recent_new(disc_path, NULL);
			if (recent->available) has += 1;
			Array_push(recents, recent);
		
			char parent_path[MAX_PATH];
			strcpy(parent_path, disc_path);
			char* tmp = strrchr(parent_path, '/') + 1;
			tmp[0] = '\0';
			Array_push(parent_paths, strdup(parent_path));
		}
		unlink(CHANGE_DISC_PATH);
	}
	
	FILE* file = fopen(RECENT_PATH, "r"); // newest at top
	if (file) {
		char line[MAX_PATH];
		while (fgets(line,MAX_PATH,file)!=NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line)==0) continue; // skip empty lines
			
			// LOG_info("line: %s\n", line);
			
			char* path = line;
			char* alias = NULL;
			char* tmp = strchr(line,'\t');
			if (tmp) {
				tmp[0] = '\0';
				alias = tmp+1;
			}
			
			char sd_path[MAX_PATH];
			snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, path); // path might come from a file, a too long one just won't exist
			if (exists(sd_path) && RecentArray_indexOf(recents, path)==-1) { // (a game twice in the file, from the old repeat, is kept once)
				if (recents->count<MAX_RECENTS) {
					// this logic replaces an existing disc from a multi-disc game with the last used
					char m3u_path[MAX_PATH];
					if (hasM3u(sd_path, m3u_path)) { // TODO: this might tank launch speed
						char parent_path[MAX_PATH];
						strcpy(parent_path, path);
						char* tmp = strrchr(parent_path, '/') + 1;
						tmp[0] = '\0';
						
						int found = 0;
						for (int i=0; i<parent_paths->count; i++) {
							char* path = parent_paths->items[i];
							if (prefixMatch(path, parent_path)) {
								found = 1;
								break;
							}
						}
						if (found) continue;
						
						Array_push(parent_paths, strdup(parent_path));
					}
					
					// LOG_info("path:%s alias:%s\n", path, alias);
					
					Recent* recent = Recent_new(path, alias);
					if (recent->available) has += 1;
					Array_push(recents, recent);
				}
			}
		}
		fclose(file);
	}
	
	saveRecents();
	
	StringArray_free(parent_paths);
	return has>0;
}
static int hasCollections(void) {
	int has = 0;
	if (!exists(COLLECTIONS_PATH)) return has;
	
	DIR *dh = opendir(COLLECTIONS_PATH);
	if (!dh) return has; // exists but isn't a readable folder
	struct dirent *dp;
	while((dp = readdir(dh)) != NULL) {
		if (hide(dp->d_name)) continue;
		has = 1;
		break;
	}
	closedir(dh);
	return has;
}
static int hasRoms(char* dir_name) {
	int has = 0;
	char emu_name[MAX_PATH];
	char rom_path[MAX_PATH];

	getEmuName(dir_name, emu_name);
	
	// check for emu pak
	if (!hasEmu(emu_name)) return has;
	
	// check for at least one non-hidden file (we're going to assume it's a rom)
	sprintf(rom_path, "%s/%s/", ROMS_PATH, dir_name);
	DIR *dh = opendir(rom_path);
	if (dh!=NULL) {
		struct dirent *dp;
		while((dp = readdir(dh)) != NULL) {
			if (hide(dp->d_name)) continue;
			has = 1;
			break;
		}
		closedir(dh);
	}
	// if (!has) printf("No roms for %s!\n", dir_name);
	return has;
}
static Array* getRoot(void) {
	Array* root = Array_new();
	
	if (hasRecents()) Array_push(root, Entry_new(FAUX_RECENT_PATH, ENTRY_DIR));
	
	Array* entries = Array_new();
	DIR* dh = opendir(ROMS_PATH);
	if (dh!=NULL) {
		struct dirent *dp;
		char* tmp;
		char full_path[MAX_PATH];
		sprintf(full_path, "%s/", ROMS_PATH);
		tmp = full_path + strlen(full_path);
		Array* emus = Array_new();
		while((dp = readdir(dh)) != NULL) {
			if (hide(dp->d_name)) continue;
			if (hasRoms(dp->d_name)) {
				strcpy(tmp, dp->d_name);
				Array_push(emus, Entry_new(full_path, ENTRY_DIR));
			}
		}
		EntryArray_sort(emus);
		Entry* prev_entry = NULL;
		for (int i=0; i<emus->count; i++) {
			Entry* entry = emus->items[i];
			if (prev_entry!=NULL) {
				if (exactMatch(prev_entry->name, entry->name)) {
					Entry_free(entry);
					continue;
				}
			}
			Array_push(entries, entry);
			prev_entry = entry;
		}
		Array_free(emus); // just free the array part, entries now owns emus entries
		closedir(dh);
	}
	
	// copied/modded from Directory_index
	// we don't support hidden remaps here
	char map_path[MAX_PATH];
	sprintf(map_path, "%s/map.txt", ROMS_PATH);
	if (entries->count>0 && exists(map_path)) {
		FILE* file = fopen(map_path, "r");
		if (file) {
			Hash* map = Hash_new();
			char line[MAX_PATH];
			while (fgets(line,MAX_PATH,file)!=NULL) {
				normalizeNewline(line);
				trimTrailingNewlines(line);
				if (strlen(line)==0) continue; // skip empty lines

				char* tmp = strchr(line,'\t');
				if (tmp) {
					tmp[0] = '\0';
					char* key = line;
					char* value = tmp+1;
					Hash_set(map, key, value);
				}
			}
			fclose(file);
			
			int resort = 0;
			for (int i=0; i<entries->count; i++) {
				Entry* entry = entries->items[i];
				char* filename = strrchr(entry->path, '/')+1;
				char* alias = Hash_get(map, filename);
				if (alias) {
					free(entry->name);
					entry->name = strdup(alias);
					resort = 1;
				}
			} 
			if (resort) EntryArray_sort(entries);
			Hash_free(map);
		}
	}
	
	if (hasCollections()) {
		if (entries->count) Array_push(root, Entry_new(COLLECTIONS_PATH, ENTRY_DIR));
		else { // no visible systems, promote collections to root
			dh = opendir(COLLECTIONS_PATH);
			if (dh!=NULL) {
				struct dirent *dp;
				char* tmp;
				char full_path[MAX_PATH];
				sprintf(full_path, "%s/", COLLECTIONS_PATH);
				tmp = full_path + strlen(full_path);
				Array* collections = Array_new();
				while((dp = readdir(dh)) != NULL) {
					if (hide(dp->d_name)) continue;
					strcpy(tmp, dp->d_name);
					Array_push(collections, Entry_new(full_path, ENTRY_DIR)); // yes, collections are fake directories
				}
				EntryArray_sort(collections);
				for (int i=0; i<collections->count; i++) {
					Array_push(entries, collections->items[i]);
				}
				Array_free(collections); // just free the array part, entries now owns collections entries
				closedir(dh);
			}
		}
	}
	
	// add systems to root
	for (int i=0; i<entries->count; i++) {
		Array_push(root, entries->items[i]);
	}
	Array_free(entries); // root now owns entries' entries
	
	char* tools_path = SDCARD_PATH "/Tools/" PLATFORM;
	if (exists(tools_path) && !simple_mode) Array_push(root, Entry_new(tools_path, ENTRY_DIR));
	
	return root;
}
static Array* getRecents(void) {
	Array* entries = Array_new();
	for (int i=0; i<recents->count; i++) {
		Recent* recent = recents->items[i];
		if (!recent->available) continue;
		
		char sd_path[MAX_PATH];
		snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, recent->path);
		int type = suffixMatch(".pak", sd_path) ? ENTRY_PAK : ENTRY_ROM; // ???
		Entry* entry = Entry_new(sd_path, type);
		if (recent->alias) {
			free(entry->name);
			entry->name = strdup(recent->alias);
		}
		Array_push(entries, entry);
	}
	return entries;
}
static Array* getCollection(char* path) {
	Array* entries = Array_new();
	FILE* file = fopen(path, "r");
	if (file) {
		char line[MAX_PATH];
		while (fgets(line,MAX_PATH,file)!=NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line)==0) continue; // skip empty lines
			
			char sd_path[MAX_PATH];
			snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, line);
			if (exists(sd_path)) {
				int type = suffixMatch(".pak", sd_path) ? ENTRY_PAK : ENTRY_ROM; // ???
				Array_push(entries, Entry_new(sd_path, type));
				
				// char emu_name[MAX_PATH];
				// getEmuName(sd_path, emu_name);
				// if (hasEmu(emu_name)) {
					// Array_push(entries, Entry_new(sd_path, ENTRY_ROM));
				// }
			}
		}
		fclose(file);
	}
	return entries;
}
static Array* getDiscs(char* path){
	
	// TODO: does path have SDCARD_PATH prefix?
	
	Array* entries = Array_new();
	
	char base_path[MAX_PATH];
	strcpy(base_path, path);
	char* tmp = strrchr(base_path, '/') + 1;
	tmp[0] = '\0';
	
	// TODO: limit number of discs supported (to 9?)
	FILE* file = fopen(path, "r");
	if (file) {
		char line[MAX_PATH];
		int disc = 0;
		while (fgets(line,MAX_PATH,file)!=NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line)==0) continue; // skip empty lines
			
			char disc_path[MAX_PATH];
			snprintf(disc_path, sizeof(disc_path), "%s%s", base_path, line);
						
			if (exists(disc_path)) {
				disc += 1;
				Entry* entry = Entry_new(disc_path, ENTRY_ROM);
				free(entry->name);
				char name[16];
				sprintf(name, "Disc %i", disc);
				entry->name = strdup(name);
				Array_push(entries, entry);
			}
		}
		fclose(file);
	}
	return entries;
}
static int getFirstDisc(char* m3u_path, char* disc_path) { // based on getDiscs() natch
	int found = 0;

	char base_path[MAX_PATH];
	strcpy(base_path, m3u_path);
	char* tmp = strrchr(base_path, '/') + 1;
	tmp[0] = '\0';
	
	FILE* file = fopen(m3u_path, "r");
	if (file) {
		char line[MAX_PATH];
		while (fgets(line,MAX_PATH,file)!=NULL) {
			normalizeNewline(line);
			trimTrailingNewlines(line);
			if (strlen(line)==0) continue; // skip empty lines
			
			snprintf(disc_path, MAX_PATH, "%s%s", base_path, line); // callers pass a MAX_PATH buffer (sizeof here would be a pointer's 8)
						
			if (exists(disc_path)) found = 1;
			break;
		}
		fclose(file);
	}
	return found;
}

static void addEntries(Array* entries, char* path) {
	DIR *dh = opendir(path);
	if (dh!=NULL) {
		struct dirent *dp;
		char* tmp;
		char full_path[MAX_PATH];
		sprintf(full_path, "%s/", path);
		tmp = full_path + strlen(full_path);
		while((dp = readdir(dh)) != NULL) {
			if (hide(dp->d_name)) continue;
			strcpy(tmp, dp->d_name);
			int is_dir = dp->d_type==DT_DIR;
			int type;
			if (is_dir) {
				// TODO: this should make sure launch.sh exists
				if (suffixMatch(".pak", dp->d_name)) {
					type = ENTRY_PAK;
				}
				else {
					type = ENTRY_DIR;
				}
			}
			else {
				if (prefixMatch(COLLECTIONS_PATH, full_path)) {
					type = ENTRY_DIR; // :shrug:
				}
				else {
					type = ENTRY_ROM;
				}
			}
			Array_push(entries, Entry_new(full_path, type));
		}
		closedir(dh);
	}
}

static int isConsoleDir(char* path) {
	char* tmp;
	char parent_dir[MAX_PATH];
	strcpy(parent_dir, path);
	tmp = strrchr(parent_dir, '/');
	tmp[0] = '\0';
	
	return exactMatch(parent_dir, ROMS_PATH);
}

static Array* getEntries(char* path){
	Array* entries = Array_new();

	if (isConsoleDir(path)) { // top-level console folder, might collate
		char collated_path[MAX_PATH];
		strcpy(collated_path, path);
		char* tmp = strrchr(collated_path, '(');
		// 1 because we want to keep the opening parenthesis to avoid collating "Game Boy Color" and "Game Boy Advance" into "Game Boy"
		// but conditional so we can continue to support a bare tag name as a folder name
		if (tmp) tmp[1] = '\0'; 
		
		DIR *dh = opendir(ROMS_PATH);
		if (dh!=NULL) {
			struct dirent *dp;
			char full_path[MAX_PATH];
			sprintf(full_path, "%s/", ROMS_PATH);
			tmp = full_path + strlen(full_path);
			// while loop so we can collate paths, see above
			while((dp = readdir(dh)) != NULL) {
				if (hide(dp->d_name)) continue;
				if (dp->d_type!=DT_DIR) continue;
				strcpy(tmp, dp->d_name);
			
				if (!prefixMatch(collated_path, full_path)) continue;
				
				// only collate folders that have an emu pak, getRoot() only checks that one of them does
				char emu_name[MAX_PATH];
				getEmuName(full_path, emu_name);
				if (!hasEmu(emu_name)) continue;
				
				addEntries(entries, full_path);
			}
			closedir(dh);
		}
	}
	else addEntries(entries, path); // just a subfolder
	
	EntryArray_sort(entries);
	return entries;
}

///////////////////////////////////////

static void queueNext(char* cmd) {
	LOG_info("cmd: %s\n", cmd);
	putFile("/tmp/next", cmd);
	quit = 1;
}

// based on https://stackoverflow.com/a/31775567/145965
static int replaceString(char *line, const char *search, const char *replace) {
   char *sp; // start of pattern
   if ((sp = strstr(line, search)) == NULL) {
      return 0;
   }
   int count = 1;
   int sLen = strlen(search);
   int rLen = strlen(replace);
   if (sLen > rLen) {
      // move from right to left
      char *src = sp + sLen;
      char *dst = sp + rLen;
      while((*dst = *src) != '\0') { dst++; src++; }
   } else if (sLen < rLen) {
      // move from left to right
      int tLen = strlen(sp) - sLen;
      char *stop = sp + rLen;
      char *src = sp + sLen + tLen;
      char *dst = sp + rLen + tLen;
      while(dst >= stop) { *dst = *src; dst--; src--; }
   }
   memcpy(sp, replace, rLen);
   count += replaceString(sp + rLen, search, replace);
   return count;
}
static char* escapeSingleQuotes(char* str) {
	// why not call replaceString directly?
	// call points require the modified string be returned
	// but replaceString is recursive and depends on its
	// own return value (but does it need to?)
	replaceString(str, "'", "'\\''");
	return str;
}

///////////////////////////////////////

static void readyResumePath(char* rom_path, int type) {
	char* tmp;
	can_resume = 0;
	char path[MAX_PATH];
	strcpy(path, rom_path);
	
	if (!prefixMatch(ROMS_PATH, path)) return;
	
	char auto_path[MAX_PATH];
	if (type==ENTRY_DIR) {
		if (!hasCue(path, auto_path)) { // no cue?
			tmp = strrchr(auto_path, '.') + 1; // extension
			strcpy(tmp, "m3u"); // replace with m3u
			if (!exists(auto_path)) return; // no m3u
		}
		strcpy(path, auto_path); // cue or m3u if one exists
	}
	
	if (!suffixMatch(".m3u", path)) {
		char m3u_path[MAX_PATH];
		if (hasM3u(path, m3u_path)) {
			// change path to m3u path
			strcpy(path, m3u_path);
		}
	}
	
	char emu_name[MAX_PATH];
	getEmuName(path, emu_name);
	
	char rom_file[MAX_PATH];
	tmp = strrchr(path, '/') + 1;
	strcpy(rom_file, tmp);
	
	sprintf(slot_path, "%s/.minui/%s/%s.txt", SHARED_USERDATA_PATH, emu_name, rom_file); // /.userdata/.minui/<EMU>/<romname>.ext.txt
	
	can_resume = exists(slot_path);
}
static void readyResume(Entry* entry) {
	readyResumePath(entry->path, entry->type);
}

static void saveLast(char* path);
static void loadLast(void);

static int autoResume(void) {
	// NOTE: bypasses recents

	if (!exists(AUTO_RESUME_PATH)) return 0;
	
	char path[MAX_PATH];
	getFile(AUTO_RESUME_PATH, path, MAX_PATH);
	unlink(AUTO_RESUME_PATH);
	sync();
	
	// make sure rom still exists
	char sd_path[MAX_PATH];
	snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, path); // path might come from a file, a too long one just won't exist
	if (!exists(sd_path)) return 0;
	
	// make sure emu still exists
	char emu_name[MAX_PATH];
	getEmuName(sd_path, emu_name);
	
	char emu_path[MAX_PATH];
	getEmuPath(emu_name, emu_path);
	
	if (!exists(emu_path)) return 0;
	
	// putFile(LAST_PATH, FAUX_RECENT_PATH); // saveLast() will crash here because top is NULL
	
	char cmd[MAX_PATH*2+16]; // two (escaped) paths, quotes and a space
	sprintf(cmd, "'%s' '%s'", escapeSingleQuotes(emu_path), escapeSingleQuotes(sd_path));
	putInt(RESUME_SLOT_PATH, AUTO_RESUME_SLOT);
	queueNext(cmd);
	return 1;
}

static void openPak(char* path) {
	// NOTE: escapeSingleQuotes() modifies the passed string 
	// so we need to save the path before we call that
	if (prefixMatch(ROMS_PATH, path)) {
		addRecent(path, NULL);
	}
	saveLast(path);
	
	char cmd[MAX_PATH*2+16]; // two (escaped) paths, quotes and a space
	sprintf(cmd, "'%s/launch.sh'", escapeSingleQuotes(path));
	queueNext(cmd);
}
static void openRom(char* path, char* last) {
	LOG_info("openRom(%s,%s)\n", path, last);
	
	char sd_path[MAX_PATH];
	strcpy(sd_path, path);
	
	char m3u_path[MAX_PATH];
	int has_m3u = hasM3u(sd_path, m3u_path);
	
	char recent_path[MAX_PATH];
	strcpy(recent_path, has_m3u ? m3u_path : sd_path);
	
	if (has_m3u && suffixMatch(".m3u", sd_path)) {
		getFirstDisc(m3u_path, sd_path);
	}

	char emu_name[MAX_PATH];
	getEmuName(sd_path, emu_name);

	if (should_resume) {
		char slot[16];
		getFile(slot_path, slot, 16);
		putFile(RESUME_SLOT_PATH, slot);
		should_resume = 0;

		if (has_m3u) {
			char rom_file[MAX_PATH];
			strcpy(rom_file, strrchr(m3u_path, '/') + 1);
			
			// get disc for state
			char disc_path_path[MAX_PATH];
			sprintf(disc_path_path, "%s/.minui/%s/%s.%s.txt", SHARED_USERDATA_PATH, emu_name, rom_file, slot); // /.userdata/arm-480/.minui/<EMU>/<romname>.ext.0.txt

			if (exists(disc_path_path)) {
				// switch to disc path
				char disc_path[MAX_PATH];
				getFile(disc_path_path, disc_path, MAX_PATH);
				if (disc_path[0]=='/') strcpy(sd_path, disc_path); // absolute
				else { // relative
					strcpy(sd_path, m3u_path);
					char* tmp = strrchr(sd_path, '/') + 1;
					snprintf(tmp, sizeof(sd_path) - (tmp - sd_path), "%s", disc_path); // a long relative path is cut, not run off the end
				}
			}
		}
	}
	else putInt(RESUME_SLOT_PATH,8); // resume hidden default state
	
	char emu_path[MAX_PATH];
	getEmuPath(emu_name, emu_path);
	
	// NOTE: escapeSingleQuotes() modifies the passed string 
	// so we need to save the path before we call that
	addRecent(recent_path, recent_alias); // yiiikes
	if (Emu_isNative(emu_path)) Playtime_save(PLAYTIME_PATH, sd_path, 0, 1, (long)time(NULL)); // minarch records its own starts; a native emulator does not, and a game never recorded keeps its New dot
	saveLast(last==NULL ? sd_path : last);
	
	char cmd[MAX_PATH*2+16]; // two (escaped) paths, quotes and a space
	sprintf(cmd, "'%s' '%s'", escapeSingleQuotes(emu_path), escapeSingleQuotes(sd_path));
	queueNext(cmd);
}
static void openDirectory(char* path, int auto_launch) {
	char auto_path[MAX_PATH];
	if (hasCue(path, auto_path) && auto_launch) {
		openRom(auto_path, path);
		return;
	}

	char m3u_path[MAX_PATH];
	strcpy(m3u_path, auto_path);
	char* tmp = strrchr(m3u_path, '.') + 1; // extension
	strcpy(tmp, "m3u"); // replace with m3u
	if (exists(m3u_path) && auto_launch) {
		auto_path[0] = '\0';
		if (getFirstDisc(m3u_path, auto_path)) {
			openRom(auto_path, path);
			return;
		}
		// TODO: doesn't handle empty m3u files
	}
	
	int selected = 0;
	int start = selected;
	int end = 0;
	if (top && top->entries->count>0) {
		if (restore_depth==stack->count && top->selected==restore_relative) {
			selected = restore_selected;
			start = restore_start;
			end = restore_end;
		}
	}
	
	top = Directory_new(path, selected);
	top->start = start;
	top->end = end ? end : ((top->entries->count<MAIN_ROW_COUNT) ? top->entries->count : MAIN_ROW_COUNT);

	Array_push(stack, top);
}
static void closeDirectory(void) {
	restore_selected = top->selected;
	restore_start = top->start;
	restore_end = top->end;
	DirectoryArray_pop(stack);
	restore_depth = stack->count;
	top = stack->items[stack->count-1];
	restore_relative = top->selected;
}

static void Entry_open(Entry* self) {
	recent_alias = self->name;  // yiiikes
	if (self->type==ENTRY_ROM) {
		char *last = NULL;
		if (prefixMatch(COLLECTIONS_PATH, top->path)) {
			char* tmp;
			char filename[MAX_PATH];
			
			tmp = strrchr(self->path, '/');
			if (tmp) strcpy(filename, tmp+1);
			
			char last_path[MAX_PATH];
			sprintf(last_path, "%s/%s", top->path, filename);
			last = last_path;
		}
		openRom(self->path, last);
	}
	else if (self->type==ENTRY_PAK) {
		openPak(self->path);
	}
	else if (self->type==ENTRY_DIR) {
		openDirectory(self->path, 1);
	}
}

///////////////////////////////////////

static void saveLast(char* path) {
	// special case for recently played
	if (exactMatch(top->path, FAUX_RECENT_PATH)) {
		// NOTE: that we don't have to save the file because
		// your most recently played game will always be at
		// the top which is also the default selection
		path = FAUX_RECENT_PATH;
	}
	putFile(LAST_PATH, path);
}
static void loadLast(void) { // call after loading root directory
	if (!exists(LAST_PATH)) return;

	char last_path[MAX_PATH];
	getFile(LAST_PATH, last_path, MAX_PATH);
	
	char full_path[MAX_PATH];
	strcpy(full_path, last_path);
	
	char* tmp;
	char filename[MAX_PATH];
	tmp = strrchr(last_path, '/');
	if (tmp) strcpy(filename, tmp);
	
	Array* last = Array_new();
	while (!exactMatch(last_path, SDCARD_PATH)) {
		Array_push(last, strdup(last_path));
		
		char* slash = strrchr(last_path, '/');
		last_path[(slash-last_path)] = '\0';
	}
	
	while (last->count>0) {
		char* path = Array_pop(last);
		if (!exactMatch(path, ROMS_PATH)) { // romsDir is effectively root as far as restoring state after a game
			char collated_path[MAX_PATH];
			collated_path[0] = '\0';
			if (suffixMatch(")", path) && isConsoleDir(path)) {
				strcpy(collated_path, path);
				tmp = strrchr(collated_path, '(');
				if (tmp) tmp[1] = '\0'; // 1 because we want to keep the opening parenthesis to avoid collating "Game Boy Color" and "Game Boy Advance" into "Game Boy"
			}
			
			for (int i=0; i<top->entries->count; i++) {
				Entry* entry = top->entries->items[i];
			
				// NOTE: strlen() is required for collated_path, '\0' wasn't reading as NULL for some reason
				if (exactMatch(entry->path, path) || (strlen(collated_path) && prefixMatch(collated_path, entry->path)) || (prefixMatch(COLLECTIONS_PATH, full_path) && suffixMatch(filename, entry->path))) {
					top->selected = i;
					if (i>=top->end) {
						top->start = i;
						top->end = top->start + MAIN_ROW_COUNT;
						if (top->end>top->entries->count) {
							top->end = top->entries->count;
							top->start = top->end - MAIN_ROW_COUNT;
						}
					}
					if (last->count==0 && !exactMatch(entry->path, FAUX_RECENT_PATH) && !(!exactMatch(entry->path, COLLECTIONS_PATH) && prefixMatch(COLLECTIONS_PATH, entry->path))) break; // don't show contents of auto-launch dirs
				
					if (entry->type==ENTRY_DIR) {
						openDirectory(entry->path, 0);
						break;
					}
				}
			}
		}
		free(path); // we took ownership when we popped it
	}
	
	StringArray_free(last);
}

///////////////////////////////////////

static void Menu_init(void) {
	stack = Array_new(); // array of open Directories
	recents = Array_new();

	openDirectory(SDCARD_PATH, 0);
	loadLast(); // restore state when available
}
static void Menu_quit(void) {
	RecentArray_free(recents);
	DirectoryArray_free(stack);
}

///////////////////////////////////////

///////////////////////////////////////
// The random button (Y): any game in the library, wherever the cursor is. It opens a random folder
// (in proportion to its games) and selects a random game there; A opens it, Y picks another.

static uint32_t random_state = 0;
static void showSelected(Directory* d, int index) { // selects it and scrolls the list so it is on screen, with some of what is around it
	int count = d->entries->count;
	d->selected = index;
	if (count>MAIN_ROW_COUNT) {
		int start = index - MAIN_ROW_COUNT/2;
		if (start<0) start = 0;
		int end = start + MAIN_ROW_COUNT;
		if (end>count) { end = count; start = end - MAIN_ROW_COUNT; }
		d->start = start;
		d->end = end;
	}
	else {
		d->start = 0;
		d->end = count;
	}
}
static void seedRandom(void) {
	if (!random_state) random_state = (uint32_t)time(NULL) ^ (SDL_GetTicks() * 2654435761u);
}
// games that have been played, by title (play time is recorded by the emulator): the rest get a dot in the launcher
static char (*played)[96] = NULL;
static int played_count = 0;
static int show_new = 1;
static void loadPlayed(void) {
	char flag[16] = "";
	getFile(SHOW_NEW_PATH, flag, sizeof(flag));
	show_new = flag[0]!='0';
	if (!show_new) return;
	char* file = allocFile(PLAYTIME_PATH);
	PlayRecord* records = file ? calloc(PLAYTIME_MAX, sizeof(PlayRecord)) : NULL;
	int count = records ? Playtime_parse(file, records, PLAYTIME_MAX) : 0;
	played = count>0 ? calloc(count, sizeof(*played)) : NULL;
	if (played) {
		for (int i=0; i<count; i++) Playtime_name(records[i].path, played[i], sizeof(played[i]));
		played_count = count;
	}
	free(records);
	free(file);
}
static int isNew(const Entry* entry) { // nothing recorded yet: nothing to compare with, so no dots
	if (!show_new || !played_count || entry->type!=ENTRY_ROM) return 0;
	char name[96];
	Playtime_name(entry->path, name, sizeof(name));
	for (int i=0; i<played_count; i++) if (!strcmp(played[i], name)) return 0;
	return 1;
}
static int pickGameHere(void) { // selects a random game in the open folder, one never played first if there is one; 1 if there was one
	Directory* d = top;
	int count = d->entries->count;
	if (count<=0) return 0;
	int* index = malloc(sizeof(int) * count);
	unsigned char* seen = malloc(count); // 1: played (a game that is new, with its dot, comes first)
	if (!index || !seen) { free(index); free(seen); return 0; }
	int n = 0;
	for (int i=0; i<count; i++) {
		Entry* entry = d->entries->items[i];
		if (entry->type!=ENTRY_ROM) continue;
		seen[n] = !isNew(entry);
		index[n++] = i;
	}
	seedRandom();
	int pick = Random_pick(seen, n, &random_state);
	int chosen = pick>=0 ? index[pick] : -1;
	free(index);
	free(seen);
	if (chosen<0) return 0;
	showSelected(d, chosen);
	return 1;
}
static int isPlainFolder(Entry* entry) { // not Recently Played, not a folder of discs that opens a game
	char cue[MAX_PATH];
	return entry->type==ENTRY_DIR && !exactMatch(entry->path, FAUX_RECENT_PATH) && !prefixMatch(COLLECTIONS_PATH, entry->path) && !prefixMatch(SDCARD_PATH "/Tools", entry->path) && !hasCue(entry->path, cue);
}
static int visibleCount(char* path) { // roughly how many games a folder holds: its entries that are not hidden
	int n = 0;
	DIR* dh = opendir(path);
	if (!dh) return 0;
	struct dirent* e;
	while ((e = readdir(dh))) if (e->d_name[0]!='.' && !suffixMatch(".txt", e->d_name)) n++;
	closedir(dh);
	return n;
}
// (MIN and MAX in defines.h have no outer parentheses, so they must not be nested or used inside an expression)
static int clampTop(int start, int count) { // the first row of a window of MAIN_ROW_COUNT rows that doesn't run past the end
	int last = count - MAIN_ROW_COUNT;
	if (last<0) last = 0;
	return start<last ? start : last;
}
// after something changed on disk (a collection): every folder on the way is read again, the cursor where it was
static void refreshStack(void) {
	restore_depth = -1; // the folder you came back from may not be what is listed there now
	for (int i=0; i<stack->count; i++) {
		Directory* old = stack->items[i];
		int old_count = old->entries->count;
		char* keep = (old_count>0 && old->selected<old_count) ? strdup(((Entry*)old->entries->items[old->selected])->path) : NULL;
		Directory* fresh = Directory_new(old->path, old->selected);
		int count = fresh->entries->count;
		if (keep) { // the same entry, wherever it is in the new list (a folder can appear above it)
			for (int j=0; j<count; j++) if (!strcmp(((Entry*)fresh->entries->items[j])->path, keep)) { fresh->selected = j; break; }
			free(keep);
		}
		if (fresh->selected>=count) fresh->selected = count>0 ? count-1 : 0;
		fresh->start = clampTop(old->start, count);
		if (fresh->selected<fresh->start) fresh->start = fresh->selected;
		fresh->end = MIN(count, fresh->start + MAIN_ROW_COUNT);
		if (fresh->selected>=fresh->end) { fresh->end = fresh->selected + 1; fresh->start = fresh->end - MAIN_ROW_COUNT; if (fresh->start<0) fresh->start = 0; }
		stack->items[i] = fresh;
		Directory_free(old);
	}
	top = stack->items[stack->count-1];
}

// Deleting a game from the launcher: the ROM file goes (and its title picture), and the game comes off Recently
// Played and out of the collections. Saves and save states are kept, so putting the game back finds them again.
// Not for multi-disc games (an .m3u or a folder of discs): those are several files, deleted on a computer.
static void removeRecent(const char* sd_path) {
	int id = RecentArray_indexOf(recents, (char*)sd_path + strlen(SDCARD_PATH));
	if (id<0) return;
	Recent_free(recents->items[id]);
	for (int i=id; i<recents->count-1; i++) recents->items[i] = recents->items[i+1];
	recents->count--;
	saveRecents();
}
static int canDelete(Entry* entry) {
	return entry->type==ENTRY_ROM && prefixMatch(ROMS_PATH, entry->path) && !suffixMatch(".pak", entry->path)
		&& !suffixMatch(".m3u", entry->path) && !suffixMatch(".cue", entry->path) && !suffixMatch(".gdi", entry->path); // (a game of several files)
}
static int deleteGame(Entry* entry) { // 1 if the file is gone
	if (!canDelete(entry)) return 0;
	if (unlink(entry->path)!=0 && exists(entry->path)) return 0;
	char art[MAX_PATH];
	const char* slash = strrchr(entry->path, '/');
	if (slash) {
		snprintf(art, sizeof(art), "%.*s/.res/%s.png", (int)(slash - entry->path), entry->path, slash+1);
		unlink(art);
	}
	removeRecent(entry->path);
	const char* rel = Collection_rel(entry->path, SDCARD_PATH);
	if (rel) {
		char names[COLLECTION_MAX][COLLECTION_NAME_MAX];
		int n = Collection_choices(COLLECTIONS_PATH, names, COLLECTION_MAX);
		for (int i=0; i<n; i++) {
			char file[MAX_PATH];
			snprintf(file, sizeof(file), "%s/%s.txt", COLLECTIONS_PATH, names[i]);
			if (Collection_contains(file, rel)) Collection_set(file, rel, 0);
		}
	}
	sync();
	return 1;
}
static int isCollectionEntry(Entry* entry) { // a collection is a .txt in Collections, listed as a folder
	return entry->type==ENTRY_DIR && !exactMatch(entry->path, COLLECTIONS_PATH) && prefixMatch(COLLECTIONS_PATH, entry->path) && suffixMatch(".txt", entry->path);
}
static int deleteCollection(Entry* entry) { // 1 if it is gone; the games in it stay where they are
	if (!isCollectionEntry(entry)) return 0;
	if (unlink(entry->path)!=0 && exists(entry->path)) return 0;
	sync();
	return 1;
}
// asks first; 1 if the game (or the collection) was deleted
static int confirmDelete(Entry* entry, SDL_Surface* screen, int* show_setting) {
	int dirty = 1, answer = 0, done = 0;
	char msg[MAX_PATH + 200];
	int is_collection = isCollectionEntry(entry);
	if (is_collection) snprintf(msg, sizeof(msg), "Delete this collection?\n%s\nThe games stay on the card.", entry->name);
	else if (canDelete(entry)) snprintf(msg, sizeof(msg), "Delete this game?\n%s\nSaves and save states are kept.", entry->name);
	else snprintf(msg, sizeof(msg), "This one can't be deleted here.\nDelete multi-file games\non a computer.");
	int can = is_collection || canDelete(entry);
	while (!done) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, show_setting, NULL, NULL);
		if (PAD_justPressed(BTN_B) || (!can && PAD_justPressed(BTN_A))) done = 1;
		else if (can && PAD_justPressed(BTN_A)) {
			answer = is_collection ? deleteCollection(entry) : deleteGame(entry);
			if (answer) done = 1;
			else { snprintf(msg, sizeof(msg), "Couldn't delete it.\nThe card may be read-only."); can = 0; dirty = 1; } // (stay: A or B closes it)
		}
		if (dirty) {
			GFX_clearUI(screen);
			GFX_blitHardwareGroup(screen, *show_setting);
			GFX_blitMessage(font.large, msg, screen, &(SDL_Rect){0, SCALE1(PADDING), screen->w, screen->h - SCALE1(PILL_SIZE+PADDING)});
			if (can) GFX_blitButtonGroup((char*[]){"B","NO","A","DELETE",NULL}, 1, screen, 1);
			else GFX_blitButtonGroup((char*[]){"A","OKAY",NULL}, 1, screen, 1);
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}
	return answer;
}

// A name for a new collection, typed on a small on-screen keyboard (X: capitals/small, Y: clear). 1 if one was entered into name.
static int askCollectionName(SDL_Surface* screen, int* show_setting, char* name, size_t cap) {
	static const char* rows_lo[4] = { "abcdefghij", "klmnopqrst", "uvwxyz0123", "456789.-'&" };
	static const char* rows_up[4] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789.-'&" };
	char text[COLLECTION_NAME_MAX] = "";
	int kx = 0, ky = 0, upper = 1, dirty = 1, result = 0, quit = 0;
	int row_h = SCALE1(PILL_SIZE);
	int w = screen->w - SCALE1(PADDING*2);
	int cell_w = w / 10;
	int top = SCALE1(PADDING) + row_h*2;
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, show_setting, NULL, NULL);
		int press = 0;
		if (PAD_justRepeated(BTN_LEFT)) { if (ky==4) kx = kx<4 ? 8 : (kx<7 ? 1 : 5); else kx = (kx+9)%10; dirty = 1; }
		else if (PAD_justRepeated(BTN_RIGHT)) { if (ky==4) kx = kx<4 ? 5 : (kx<7 ? 8 : 1); else kx = (kx+1)%10; dirty = 1; }
		else if (PAD_justRepeated(BTN_UP)) { ky = (ky+4)%5; dirty = 1; }
		else if (PAD_justRepeated(BTN_DOWN)) { ky = (ky+1)%5; dirty = 1; }
		else if (PAD_justPressed(BTN_A)) press = 1;
		else if (PAD_justPressed(BTN_X)) { upper = !upper; dirty = 1; }
		else if (PAD_justPressed(BTN_Y)) { text[0] = '\0'; dirty = 1; }
		else if (PAD_justPressed(BTN_START)) { ky = 4; kx = 8; press = 1; }
		else if (PAD_justPressed(BTN_B)) { size_t n = strlen(text); if (n) { text[n-1] = '\0'; dirty = 1; } else quit = 1; }
		if (press) {
			size_t n = strlen(text);
			dirty = 1;
			if (ky<4) { if (n+1<sizeof(text)) { text[n] = (upper ? rows_up : rows_lo)[ky][kx]; text[n+1] = '\0'; } }
			else {
				int b = kx<4 ? 0 : (kx<7 ? 1 : 2);
				if (b==0) { if (n+1<sizeof(text)) { text[n] = ' '; text[n+1] = '\0'; } }
				else if (b==1) { if (n) text[n-1] = '\0'; }
				else {
					while (n && text[n-1]==' ') text[--n] = '\0';
					const char* t = text;
					while (*t==' ') t++;
					if (*t) { snprintf(name, cap, "%s", t); result = 1; quit = 1; }
				}
			}
		}
		if (dirty && !quit) {
			GFX_clearUI(screen);
			GFX_blitPill(ASSET_DARK_GRAY_PILL, screen, &(SDL_Rect){SCALE1(PADDING), SCALE1(PADDING), w, row_h});
			char shown[COLLECTION_NAME_MAX+2], out[256];
			snprintf(shown, sizeof(shown), "%s_", text);
			GFX_truncateText(font.large, shown, out, w - SCALE1(BUTTON_PADDING*2), 0);
			SDL_Surface* t = GFX_renderText(font.large, out, COLOR_WHITE);
			if (t) { SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){SCALE1(PADDING+BUTTON_PADDING), SCALE1(PADDING)+(row_h-t->h)/2}); SDL_FreeSurface(t); }
			for (int r=0; r<4; r++) for (int c=0; c<10; c++) {
				int x = SCALE1(PADDING) + c*cell_w, y = top + r*row_h;
				int on = ky==r && kx==c;
				if (on) GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){x+2, y, cell_w-4, row_h});
				char ch[2] = { (upper ? rows_up : rows_lo)[r][c], 0 };
				SDL_Surface* s = GFX_renderText(font.large, ch, on ? COLOR_BLACK : COLOR_WHITE);
				if (s) { SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){x+(cell_w-s->w)/2, y+(row_h-s->h)/2}); SDL_FreeSurface(s); }
			}
			const char* labels[3] = { "SPACE", "DELETE", "ADD" };
			int starts[3] = { 0, 4, 7 }, widths[3] = { 4, 3, 3 };
			int cur = kx<4 ? 0 : (kx<7 ? 1 : 2);
			for (int b=0; b<3; b++) {
				int x = SCALE1(PADDING) + starts[b]*cell_w, y = top + 4*row_h + row_h/2, ww = widths[b]*cell_w - 4;
				int on = ky==4 && cur==b;
				GFX_blitPill(on ? ASSET_WHITE_PILL : ASSET_DARK_GRAY_PILL, screen, &(SDL_Rect){x+2, y, ww, row_h});
				SDL_Surface* s = GFX_renderText(font.medium, labels[b], on ? COLOR_BLACK : COLOR_WHITE);
				if (s) { SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){x+2+(ww-s->w)/2, y+(row_h-s->h)/2}); SDL_FreeSurface(s); }
			}
			GFX_blitButtonGroup((char*[]){"X","ABC/abc","Y","CLEAR",NULL}, 0, screen, 0);
			GFX_blitButtonGroup((char*[]){"B","DELETE","A","TYPE",NULL}, 1, screen, 1);
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}
	return result;
}
// START on a game: the collections, A puts the game in one or takes it out; the last row deletes the game. 1 if anything changed.
static int collectionPage(Entry* entry, SDL_Surface* screen, int* show_setting) {
	const char* rel = Collection_rel(entry->path, SDCARD_PATH);
	if (!rel) return 0;
	char names[COLLECTION_MAX][COLLECTION_NAME_MAX];
	char files[COLLECTION_MAX][MAX_PATH];
	int in[COLLECTION_MAX];
	int count = Collection_choices(COLLECTIONS_PATH, names, COLLECTION_MAX);
	for (int i=0; i<count; i++) {
		snprintf(files[i], MAX_PATH, "%s/%s.txt", COLLECTIONS_PATH, names[i]);
		in[i] = Collection_contains(files[i], rel);
	}
	int row_h = SCALE1(PILL_SIZE);
	int top_y = SCALE1(PADDING) + row_h;
	int visible = (screen->h - SCALE1(PADDING+PILL_SIZE) - SCALE1(8) - top_y) / row_h;
	if (visible<1) visible = 1;
	int sel = 0, first = 0, changed = 0, dirty = 1, quit = 0;
	int rows = count + 2; // the collections, New collection, then Delete game
	char message[64] = "";
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, show_setting, NULL, NULL);
		if (PAD_justPressed(BTN_B) || PAD_justPressed(BTN_START)) quit = 1;
		else if (PAD_justRepeated(BTN_UP)) { sel = (sel + rows - 1) % rows; dirty = 1; message[0] = '\0'; }
		else if (PAD_justRepeated(BTN_DOWN)) { sel = (sel + 1) % rows; dirty = 1; message[0] = '\0'; }
		else if (PAD_justPressed(BTN_A) && sel==count) { // New collection: the game goes straight into it
			char fresh[COLLECTION_NAME_MAX];
			int named = askCollectionName(screen, show_setting, fresh, sizeof(fresh)) && !strchr(fresh, '/');
			if (named && (fresh[0]=='.' || !strcasecmp(fresh, "map") || count>=COLLECTION_MAX)) { // (the launcher hides those: the game would go somewhere nobody can see)
				snprintf(message, sizeof(message), "Pick another name");
				named = 0;
			}
			if (named) {
				char file[MAX_PATH];
				snprintf(file, sizeof(file), "%s/%s.txt", COLLECTIONS_PATH, fresh);
				if (Collection_set(file, rel, 1)==0) { changed = 1; quit = 1; }
				else snprintf(message, sizeof(message), "Could not save %s", fresh);
			}
			dirty = 1;
		}
		else if (PAD_justPressed(BTN_A) && sel==count+1) { // Delete game
			if (confirmDelete(entry, screen, show_setting)) { changed = 1; quit = 1; }
			dirty = 1;
		}
		else if (PAD_justPressed(BTN_A) && count) {
			int want = !in[sel];
			if (Collection_set(files[sel], rel, want)==0) {
				in[sel] = want;
				changed = 1;
				snprintf(message, sizeof(message), "%s %s", want ? "Added to" : "Removed from", names[sel]);
			}
			else snprintf(message, sizeof(message), "Could not save %s", names[sel]);
			dirty = 1;
		}
		if (sel<first) first = sel;
		if (sel>=first+visible) first = sel - visible + 1;
		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, *show_setting);
			char title[MAX_PATH], out[MAX_PATH];
			snprintf(title, sizeof(title), "%s", entry->name);
			GFX_truncateText(font.large, title, out, screen->w - SCALE1(PADDING*2) - ow, SCALE1(BUTTON_PADDING*2));
			SDL_Surface* t = GFX_renderText(font.large, out, COLOR_LIGHT_TEXT);
			if (t) { SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){SCALE1(PADDING+BUTTON_PADDING), SCALE1(PADDING)+4}); SDL_FreeSurface(t); }
			int w = screen->w - SCALE1(PADDING*2);
			for (int i=0; i<visible && first+i<rows; i++) {
				int y = top_y + i*row_h;
				int selected = first+i==sel;
				if (selected) GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), y, w, row_h});
				if (first+i>=count) { // the last two rows
					SDL_Surface* d = GFX_renderText(font.large, first+i==count ? "New collection..." : "Delete game...", selected ? COLOR_BLACK : COLOR_LIGHT_TEXT);
					if (d) { SDL_BlitSurface(d, NULL, screen, &(SDL_Rect){SCALE1(PADDING+BUTTON_PADDING), y+(row_h-d->h)/2}); SDL_FreeSurface(d); }
					continue;
				}
				char name[256]; // (GFX_truncateText wants 256)
				GFX_truncateText(font.large, names[first+i], name, w * 6 / 10, SCALE1(BUTTON_PADDING*2));
				SDL_Surface* n = GFX_renderText(font.large, name, selected ? COLOR_BLACK : COLOR_WHITE);
				if (n) { SDL_BlitSurface(n, NULL, screen, &(SDL_Rect){SCALE1(PADDING+BUTTON_PADDING), y+(row_h-n->h)/2}); SDL_FreeSurface(n); }
				SDL_Surface* v = GFX_renderText(font.medium, in[first+i] ? "In" : "-", selected ? COLOR_BLACK : COLOR_LIGHT_TEXT);
				if (v) { SDL_BlitSurface(v, NULL, screen, &(SDL_Rect){SCALE1(PADDING)+w-v->w-SCALE1(BUTTON_PADDING), y+(row_h-v->h)/2}); SDL_FreeSurface(v); }
			}
			if (message[0]) {
				SDL_Surface* m = GFX_renderText(font.small, message, COLOR_LIGHT_TEXT);
				if (m) { SDL_BlitSurface(m, NULL, screen, &(SDL_Rect){SCALE1(PADDING+BUTTON_PADDING), top_y + (visible<rows ? visible : rows)*row_h + SCALE1(4)}); SDL_FreeSurface(m); }
			}
			GFX_blitButtonGroup((char*[]){"B","BACK","A", sel==count ? "NEW" : sel==count+1 ? "DELETE" : in[sel] ? "REMOVE" : "ADD", NULL}, 1, screen, 1);
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}
	return changed;
}
static int jumpToTools(void) { // SELECT: the Tools folder from anywhere. 1 if it opened; 0 changes nothing (no Tools folder at the top)
	Directory* root = stack->items[0];
	int at = -1;
	for (int i=0; i<root->entries->count && at<0; i++) if (!strcmp(((Entry*)root->entries->items[i])->path, SDCARD_PATH "/Tools/" PLATFORM)) at = i;
	if (at<0) return 0;
	while (stack->count>1) closeDirectory();
	Entry* entry = top->entries->items[at];
	top->selected = at;
	if (at<top->start || at>=top->end) {
		top->start = clampTop(at, top->entries->count);
		top->end = MIN(top->entries->count, top->start + MAIN_ROW_COUNT);
	}
	openDirectory(entry->path, 0);
	return 1;
}
// Y: a random game from the whole library (a random folder at the root or in a folder of folders, in proportion to what
// it holds). 1 if it found one: the selection, and maybe the open folder, changed. 0: none anywhere, and the launcher is
// left at the top (the way this starts is by going there).
static int jumpToRandomGame(void) {
	while (stack->count>1) closeDirectory(); // from the top, wherever the cursor was: it picks from the whole library
	int depth = stack->count;
	if (pickGameHere()) return 1;
	for (int attempt=0; attempt<8; attempt++) {
		Directory* d = top;
		// a folder is picked in proportion to its games, so one lonely PlayStation game isn't as likely as a whole library
		int* weight = calloc(d->entries->count>0 ? d->entries->count : 1, sizeof(int));
		if (!weight) break;
		long total = 0;
		for (int i=0; i<d->entries->count; i++) if (isPlainFolder(d->entries->items[i])) { weight[i] = visibleCount(((Entry*)d->entries->items[i])->path); total += weight[i]; }
		if (!total) { free(weight); break; }
		seedRandom();
		long pick = Random_pick(NULL, (int)total, &random_state);
		int at = -1;
		for (int i=0; i<d->entries->count && at<0; i++) { if (pick<weight[i]) at = i; else pick -= weight[i]; }
		free(weight);
		if (at<0) break;
		Entry* folder = d->entries->items[at];
		showSelected(d, at); // so Back returns to the folder it came from, in view
		openDirectory(folder->path, 0); // 0: a folder of discs must not start a game
		if (stack->count<=depth || top==d) break; // it didn't open
		if (pickGameHere()) return 1;
		int has_folders = 0;
		for (int i=0; i<top->entries->count; i++) if (isPlainFolder(top->entries->items[i])) { has_folders = 1; break; }
		if (!has_folders) closeDirectory(); // empty: back, and try another
	}
	while (stack->count>depth) closeDirectory(); // nothing found: where it started
	return 0;
}

int main (int argc, char *argv[]) {
	// LOG_info("time from launch to:\n");
	// unsigned long main_begin = SDL_GetTicks();
	// unsigned long first_draw = 0;
	
	if (autoResume()) { unlink(SWITCH_PATH); unlink(SWITCHER_REQUEST_PATH); unlink(SWITCHER_BACK_PATH); return 0; } // (a request left behind must not start a game on some later boot)
	
	simple_mode = exists(SIMPLE_MODE_PATH);
	loadPlayed();

	LOG_info("MinUI\n");
	InitSettings();
	
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	// LOG_info("- graphics init: %lu\n", SDL_GetTicks() - main_begin);
	
	PAD_init();
	// LOG_info("- input init: %lu\n", SDL_GetTicks() - main_begin);
	
	PWR_init();
	if (!HAS_POWER_BUTTON && !simple_mode) PWR_disableSleep();
	// LOG_info("- power init: %lu\n", SDL_GetTicks() - main_begin);
	
	
	Menu_init();
	// LOG_info("- menu init: %lu\n", SDL_GetTicks() - main_begin);
	
	// now that (most of) the heavy lifting is done, take a load off
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	GFX_setVsync(VSYNC_STRICT);

	PAD_reset();
	int dirty = 1;
	int marquee_active = 0; // the selected name is wider than its pill and is scrolling (redraw every frame)
	Entry* marquee_entry = NULL;
	uint32_t marquee_start = 0;
	char thumb_path[MAX_PATH] = ""; // the picture shown, kept so it isn't decoded again on every redraw
	SDL_Surface* thumb_cache = NULL;
	int show_version = 0;
	int about_first = 0; // first row showing on the Fn page
	uint32_t about_at = 0;
	int show_setting = 0; // 1=brightness,2=volume
	int was_online = PLAT_isOnline();
	
	// Fn twice in a game left the game in SWITCHER_REQUEST_PATH: the Switcher is opened below, and the game is kept in
	// SWITCHER_BACK_PATH. When the launcher starts again and the Switcher chose nothing (B), the game is resumed.
	char back_game[MAX_PATH+16] = "";
	int switcher_requested = exists(SWITCHER_REQUEST_PATH);
	if (switcher_requested) {
		getFile(SWITCHER_REQUEST_PATH, back_game, sizeof(back_game));
		unlink(SWITCHER_REQUEST_PATH);
		{ // (only when the Switcher is there to open: a back file with nothing opened would resume the game on some later start)
			char switcher_launch[MAX_PATH];
			snprintf(switcher_launch, sizeof(switcher_launch), "%s/Tools/%s/Switcher.pak/launch.sh", SDCARD_PATH, PLATFORM);
			if (back_game[0]=='/' && exists(switcher_launch)) putFile(SWITCHER_BACK_PATH, back_game);
		}
	}
	else if (exists(SWITCHER_BACK_PATH)) {
		if (!exists(SWITCH_PATH)) rename(SWITCHER_BACK_PATH, SWITCH_PATH); // nothing was chosen: back to the game
		else unlink(SWITCHER_BACK_PATH);
	}
	
	if (exists(SWITCH_PATH)) { // Tools > Switcher picked a game: open it like a press of X (Resume) or A on it
		char rel[MAX_PATH] = "";
		getFile(SWITCH_PATH, rel, sizeof(rel));
		unlink(SWITCH_PATH);
		rel[strcspn(rel, "\r\n")] = '\0';
		int slot = -1; // "<path>\t<slot>": the save slot to resume from (the Switcher says which: its newest state)
		char* tab = strchr(rel, '\t');
		if (tab) { *tab = '\0'; slot = atoi(tab+1); if (slot<0 || slot>9) slot = -1; }
		char sd_path[MAX_PATH], emu_name[MAX_PATH], emu_path[MAX_PATH];
		snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, rel);
		getEmuName(sd_path, emu_name);
		getEmuPath(emu_name, emu_path);
		if (rel[0] && exists(sd_path) && prefixMatch(ROMS_PATH, sd_path) && exists(emu_path)) {
			readyResumePath(sd_path, ENTRY_ROM);
			should_resume = can_resume;
			char m3u[MAX_PATH];
			int several_discs = suffixMatch(".m3u", sd_path) || hasM3u(sd_path, m3u);
			openRom(sd_path, NULL);
			// openRom decided on the launcher's own slot; the Switcher knows which state is newest. A multi-disc game keeps
			// the launcher's slot when it has one: that is the slot whose disc openRom just chose.
			if (slot>=0 && !(several_discs && can_resume)) putInt(RESUME_SLOT_PATH, slot);
		}
	}
	
	if (switcher_requested) { // Fn twice in a game: minarch saved the game; on to the switcher
		char switcher[MAX_PATH];
		snprintf(switcher, sizeof(switcher), "%s/Tools/%s/Switcher.pak", SDCARD_PATH, PLATFORM);
		char switcher_launch[MAX_PATH];
		snprintf(switcher_launch, sizeof(switcher_launch), "%s/launch.sh", switcher);
		if (exists(switcher_launch)) openPak(switcher);
	}
	
	// LOG_info("- loop start: %lu\n", SDL_GetTicks() - main_begin);
	while (!quit) {
		GFX_startFrame();
		unsigned long now = SDL_GetTicks();
		
		PAD_poll();
			
		int selected = top->selected;
		int total = top->entries->count;
		
		PWR_update(&dirty, &show_setting, NULL, NULL);
		
		int is_online = PLAT_isOnline();
		if (was_online!=is_online) dirty = 1;
		was_online = is_online;
		if (GFX_clockChanged()) dirty = 1;
		
		if (show_version) {
			int about_visible = InfoView_visibleRows(screen), about_rows = InfoView_rowCount();
			if (PAD_justPressed(BTN_B) || PAD_tappedMenu(now)) {
				show_version = 0;
				dirty = 1;
				if (!HAS_POWER_BUTTON && !simple_mode) PWR_disableSleep();
			}
			else if (PAD_justRepeated(BTN_UP) && about_first>0) { about_first--; dirty = 1; }
			else if (PAD_justRepeated(BTN_DOWN) && about_first+about_visible<about_rows) { about_first++; dirty = 1; }
			else if (PAD_justRepeated(BTN_LEFT) && about_first>0) { about_first = MAX(0, about_first-about_visible); dirty = 1; }
			else if (PAD_justRepeated(BTN_RIGHT) && about_first+about_visible<about_rows) { about_first = MIN(about_rows-about_visible, about_first+about_visible); dirty = 1; }
			else if (PAD_justPressed(BTN_X)) { InfoView_toggleWifi(); about_at = now; dirty = 1; }
			else if (PAD_justPressed(BTN_A)) { InfoView_forceWifi(); InfoView_gather(); about_at = now; dirty = 1; }
			if (InfoView_poll()) dirty = 1;
			if (show_version && now-about_at>3000) { InfoView_gather(); about_at = now; dirty = 1; }
		}
		else {
			char switcher[MAX_PATH];
			snprintf(switcher, sizeof(switcher), "%s/Tools/%s/Switcher.pak", SDCARD_PATH, PLATFORM);
			int fn_tapped = PAD_tappedMenu(now);
			if (fn_tapped && exists(switcher)) openPak(switcher); // Fn: the game switcher (the Info page is Tools > Info)
			else if (fn_tapped) { // no Switcher on this card: the Info page, as before
				show_version = 1;
				about_first = 0;
				InfoView_forceWifi();
				InfoView_gather();
				about_at = now;
				dirty = 1;
				if (!HAS_POWER_BUTTON && !simple_mode) PWR_enableSleep();
			}
			else if (total>0) {
				if (PAD_justRepeated(BTN_UP)) {
					if (selected==0 && !PAD_justPressed(BTN_UP)) {
						// stop at top
					}
					else {
						selected -= 1;
						if (selected<0) {
							selected = total-1;
							int start = total - MAIN_ROW_COUNT;
							top->start = (start<0) ? 0 : start;
							top->end = total; 
						}
						else if (selected<top->start) {
							top->start -= 1;
							top->end -= 1;
						}
					}
				}
				else if (PAD_justRepeated(BTN_DOWN)) {
					if (selected==total-1 && !PAD_justPressed(BTN_DOWN)) {
						// stop at bottom
					}
					else {
						selected += 1;
						if (selected>=total) {
							selected = 0;
							top->start = 0;
							top->end = (total<MAIN_ROW_COUNT) ? total : MAIN_ROW_COUNT;
						}
						else if (selected>=top->end) {
							top->start += 1;
							top->end += 1;
						}
					}
				}
				if (PAD_justRepeated(BTN_LEFT)) {
					selected -= MAIN_ROW_COUNT;
					if (selected<0) {
						selected = 0;
						top->start = 0;
						top->end = (total<MAIN_ROW_COUNT) ? total : MAIN_ROW_COUNT;
					}
					else if (selected<top->start) {
						top->start -= MAIN_ROW_COUNT;
						if (top->start<0) top->start = 0;
						top->end = top->start + MAIN_ROW_COUNT;
					}
				}
				else if (PAD_justRepeated(BTN_RIGHT)) {
					selected += MAIN_ROW_COUNT;
					if (selected>=total) {
						selected = total-1;
						int start = total - MAIN_ROW_COUNT;
						top->start = (start<0) ? 0 : start;
						top->end = total;
					}
					else if (selected>=top->end) {
						top->end += MAIN_ROW_COUNT;
						if (top->end>total) top->end = total;
						top->start = top->end - MAIN_ROW_COUNT;
					}
				}
			}
		
			if (PAD_justRepeated(BTN_L1) && !PAD_isPressed(BTN_R1) && !PWR_ignoreSettingInput(BTN_L1, show_setting)) { // previous alpha
				Entry* entry = top->entries->items[selected];
				int i = entry->alpha-1;
				if (i>=0) {
					selected = top->alphas->items[i];
					if (total>MAIN_ROW_COUNT) {
						top->start = selected;
						top->end = top->start + MAIN_ROW_COUNT;
						if (top->end>total) top->end = total;
						top->start = top->end - MAIN_ROW_COUNT;
					}
				}
			}
			else if (PAD_justRepeated(BTN_R1) && !PAD_isPressed(BTN_L1) && !PWR_ignoreSettingInput(BTN_R1, show_setting)) { // next alpha
				Entry* entry = top->entries->items[selected];
				int i = entry->alpha+1;
				if (i<top->alphas->count) {
					selected = top->alphas->items[i];
					if (total>MAIN_ROW_COUNT) {
						top->start = selected;
						top->end = top->start + MAIN_ROW_COUNT;
						if (top->end>total) top->end = total;
						top->start = top->end - MAIN_ROW_COUNT;
					}
				}
			}
	
			if (PAD_justPressed(BTN_START) && total>0) { // put the game in a collection
				Entry* entry = top->entries->items[top->selected];
				if (entry->type==ENTRY_ROM && collectionPage(entry, screen, &show_setting)) {
					refreshStack();
					total = top->entries->count;
					selected = top->selected;
				}
				else if (isCollectionEntry(entry) && confirmDelete(entry, screen, &show_setting)) { // START on a collection: delete it
					refreshStack();
					if (top->entries->count==0 && stack->count>1) closeDirectory(); // (the last one: leave the empty list)
					total = top->entries->count;
					selected = top->selected;
				}
				PAD_reset(); // the A that answered the page must not also open the game now under the cursor
				dirty = 1;
			}
	
			if (PAD_justPressed(BTN_SELECT)) { // Tools
				if (jumpToTools()) {
					total = top->entries->count;
					selected = top->selected;
					dirty = 1;
				}
			}
	
			if (PAD_justPressed(BTN_Y)) { // a random game
				jumpToRandomGame();
				total = top->entries->count; // (the folder may have changed)
				selected = top->selected;
				dirty = 1;
			}
	
			if (selected!=top->selected) {
				top->selected = selected;
				dirty = 1;
			}
	
			if (dirty && total>0) readyResume(top->entries->items[top->selected]);

			if (total>0 && can_resume && PAD_justReleased(BTN_RESUME)) {
				should_resume = 1;
				Entry_open(top->entries->items[top->selected]);
				dirty = 1;
			}
			else if (total>0 && PAD_justPressed(BTN_A)) {
				Entry_open(top->entries->items[top->selected]);
				total = top->entries->count;
				dirty = 1;

				if (total>0) readyResume(top->entries->items[top->selected]);
			}
			else if (PAD_justPressed(BTN_B) && stack->count>1) {
				closeDirectory();
				total = top->entries->count;
				dirty = 1;
				// can_resume = 0;
				if (total>0) readyResume(top->entries->items[top->selected]);
			}
		}
		
		if (marquee_active) dirty = 1;
		if (dirty) {
			marquee_active = 0;
			GFX_clearUI(screen);
			
			int ox = 0;
			int oy = 0;
			
			// simple thumbnail support a thumbnail for a file or folder named NAME.EXT needs a corresponding /.res/NAME.EXT.png 
			// that is no bigger than platform FIXED_HEIGHT x FIXED_HEIGHT
			int had_thumb = 0;
			char art_flag[8] = "";
			getFile(SHOW_ART_PATH, art_flag, sizeof(art_flag));
			int art_on = art_flag[0]!='0'; // Tools > Settings and Tools > Title Art switch it
			if (!show_version && total>0) {
				Entry* entry = top->entries->items[top->selected];
				char res_path[MAX_PATH];
				
				char res_root[MAX_PATH];
				strcpy(res_root, entry->path);
				
				char tmp_path[MAX_PATH];
				strcpy(tmp_path, entry->path);
				char* res_name = strrchr(tmp_path, '/') + 1;

				char* tmp = strrchr(res_root, '/');
				tmp[0] = '\0';
				
				sprintf(res_path, "%s/.res/%s.png", res_root, res_name);
				SDL_Surface* thumb = NULL;
				if (art_on) {
					if (strcmp(thumb_path, res_path)) { // a different picture: load it (a file that isn't a picture is no picture)
						if (thumb_cache) SDL_FreeSurface(thumb_cache);
						thumb_cache = exists(res_path) ? IMG_Load(res_path) : NULL;
						snprintf(thumb_path, sizeof(thumb_path), "%s", res_path);
					}
					thumb = thumb_cache;
				}
				if (thumb) {
					had_thumb = 1;
					ox = MAX(FIXED_WIDTH - FIXED_HEIGHT, (FIXED_WIDTH - thumb->w));
					oy = (FIXED_HEIGHT - thumb->h) / 2;
					SDL_BlitSurface(thumb, NULL, screen, &(SDL_Rect){ox,oy});
				}
			}
			
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			
			if (show_version) {
				// the Fn page is the same screen as Tools > Info
				InfoView_draw(screen, about_first, ow);
				
				// buttons (duped and trimmed from below)
				if (show_setting && !GetHDMI()) GFX_blitHardwareHints(screen, show_setting);
				else GFX_blitButtonGroup((char*[]){ BTN_SLEEP==BTN_POWER?"POWER":"MENU","SLEEP",  NULL }, 0, screen, 0);
				
				GFX_blitButtonGroup((char*[]){ "X",(char*)InfoView_wifiHint(),"B","BACK",  NULL }, 0, screen, 1);
			}
			else {
				// list
				if (total>0) {
					int selected_row = top->selected - top->start;
					for (int i=top->start,j=0; i<top->end; i++,j++) {
						Entry* entry = top->entries->items[i];
						char* entry_name = entry->name;
						char* entry_unique = entry->unique;
						if (isNew(entry)) { // a dot in the left margin
							int d = SCALE1(6);
							GFX_fillCapsule(screen, (SDL_Rect){(SCALE1(PADDING)-d)/2, SCALE1(PADDING+(j*PILL_SIZE)) + (SCALE1(PILL_SIZE)-d)/2, d, d}, SDL_MapRGB(screen->format, 255,255,255));
						}
						// with a picture showing, every row (the selected one too) stops short of it
						int available_width = (had_thumb ? ox : screen->w) - SCALE1(PADDING * 2);
						if (i==top->start && !had_thumb) available_width -= ow; // 
					
						SDL_Color text_color = COLOR_WHITE;
					
						trimSortingMeta(&entry_name);
					
						char display_name[MAX_PATH];
						char unique_name[MAX_PATH];
						int text_width;
						if (entry_unique) {
							trimSortingMeta(&entry_unique);
							text_width = GFX_truncateUniqueText(font.large, entry_name, entry_unique, display_name, unique_name, available_width, SCALE1(BUTTON_PADDING*2));
						}
						else text_width = GFX_truncateText(font.large, entry_name, display_name, available_width, SCALE1(BUTTON_PADDING*2));
						int max_width = MIN(available_width, text_width);
						int selected_full = 0;
						if (j==selected_row) {
							GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){
								SCALE1(PADDING),
								SCALE1(PADDING+(j*PILL_SIZE)),
								max_width,
								SCALE1(PILL_SIZE)
							});
							text_color = COLOR_BLACK;
							if (entry_unique) strcpy(display_name, unique_name); // the selected row shows all of it
							selected_full = 1; // ... and when that is wider than the pill, it scrolls (below)
						}
						else if (entry_unique) {
							SDL_Surface* text = GFX_renderText(font.large, unique_name, COLOR_DARK_TEXT);
							SDL_BlitSurface(text, &(SDL_Rect){
								0,
								0,
								max_width-SCALE1(BUTTON_PADDING*2),
								text->h
							}, screen, &(SDL_Rect){
								SCALE1(PADDING+BUTTON_PADDING),
								SCALE1(PADDING+(j*PILL_SIZE)+4)
							});
							SDL_FreeSurface(text);
						}
						// the selected name is drawn whole only when it really doesn't fit next to the picture (then it
						// scrolls); otherwise as the pill was sized (a tag without its file extension, for duplicates)
						const char* full_text = entry_unique ? entry_unique : entry_name;
						int too_wide = selected_full && GFX_textWidth(font.large, full_text) > available_width - SCALE1(BUTTON_PADDING*2);
						SDL_Surface* text = GFX_renderText(font.large, too_wide ? full_text : display_name, text_color);
						int clip_w = max_width-SCALE1(BUTTON_PADDING*2);
						int src_x = 0;
						if (too_wide && text->w > clip_w) {
							// it doesn't fit: scroll it, with a pause at each end
							uint32_t now = SDL_GetTicks();
							if (marquee_entry!=entry) { marquee_entry = entry; marquee_start = now; }
							int overflow = text->w - clip_w;
							uint32_t pause = 800;
							uint32_t travel = (uint32_t)overflow * 1000 / SCALE1(50); // 50 px a second before scaling
							uint32_t t = (now - marquee_start) % (pause + travel + pause);
							src_x = t<pause ? 0 : t<pause+travel ? (int)((t-pause) * SCALE1(50) / 1000) : overflow;
							marquee_active = 1;
						}
						SDL_BlitSurface(text, &(SDL_Rect){
							src_x,
							0,
							clip_w,
							text->h
						}, screen, &(SDL_Rect){
							SCALE1(PADDING+BUTTON_PADDING),
							SCALE1(PADDING+(j*PILL_SIZE)+4)
						});
						SDL_FreeSurface(text);
					}
				}
				else {
					// TODO: for some reason screen's dimensions end up being 0x0 in GFX_blitMessage...
					GFX_blitMessage(font.large, "Empty folder", screen, &(SDL_Rect){0,0,screen->w,screen->h}); //, NULL);
				}
			
				// buttons
				if (show_setting && !GetHDMI()) GFX_blitHardwareHints(screen, show_setting);
				else if (can_resume) GFX_blitButtonGroup((char*[]){ "X","RESUME",  NULL }, 0, screen, 0);
				else if (total>0 && isCollectionEntry(top->entries->items[top->selected])) GFX_blitButtonGroup((char*[]){ "START","DELETE",  NULL }, 0, screen, 0); // (a collection: the way to delete it)
				else GFX_blitButtonGroup((char*[]){ 
					BTN_SLEEP==BTN_POWER?"POWER":"MENU",
					BTN_SLEEP==BTN_POWER||simple_mode?"SLEEP":"INFO",  
					NULL }, 0, screen, 0);
			
				if (total==0) {
					if (stack->count>1) {
						GFX_blitButtonGroup((char*[]){ "B","BACK",  NULL }, 0, screen, 1);
					}
				}
				else {
					if (stack->count>1) {
						GFX_blitButtonGroup((char*[]){ "B","BACK", "A","OPEN", NULL }, 1, screen, 1);
					}
					else {
						GFX_blitButtonGroup((char*[]){ "Y","RANDOM", "A","OPEN", NULL }, 0, screen, 1);
					}
				}
			}

			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
		
		// if (!first_draw) {
		// 	first_draw = SDL_GetTicks();
		// 	LOG_info("- first draw: %lu\n", first_draw - main_begin);
		// }
		
		// handle HDMI change
		static int had_hdmi = -1;
		int has_hdmi = GetHDMI();
		if (had_hdmi==-1) had_hdmi = has_hdmi;
		if (has_hdmi!=had_hdmi) {
			had_hdmi = has_hdmi;

			Entry* entry = top->entries->items[top->selected];
			LOG_info("restarting after HDMI change... (%s)\n", entry->path);
			saveLast(entry->path); // NOTE: doesn't work in Recents (by design)
			sleep(4);
			quit = 1;
		}
	}
	
	if (thumb_cache) SDL_FreeSurface(thumb_cache);

	Menu_quit();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	QuitSettings();
}