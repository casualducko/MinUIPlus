#define _GNU_SOURCE // for strcasestr
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include "defines.h"
#include "utils.h"

///////////////////////////////////////

int prefixMatch(char* pre, char* str) {
	return (strncasecmp(pre,str,strlen(pre))==0);
}
int suffixMatch(char* suf, char* str) {
	int len = strlen(suf);
	int offset = strlen(str)-len;
	return (offset>=0 && strncasecmp(suf, str+offset, len)==0);
}
int exactMatch(char* str1, char* str2) {
	if (!str1 || !str2) return 0; // NULL isn't safe here
	int len1 = strlen(str1);
	if (len1!=strlen(str2)) return 0;
	return (strncmp(str1,str2,len1)==0);
}
int containsString(char* haystack, char* needle) {
	return strcasestr(haystack, needle) != NULL;
}
int hide(char* file_name) {
	return file_name[0]=='.' || suffixMatch(".disabled", file_name) || exactMatch("map.txt", file_name);
}

void getDisplayName(const char* in_name, char* out_name) {
	char* tmp;
	char work_name[MAX_PATH];
	strcpy(work_name, in_name);
	strcpy(out_name, in_name);
	
	if (suffixMatch("/" PLATFORM, work_name)) { // hide platform from Tools path...
		tmp = strrchr(work_name, '/');
		tmp[0] = '\0';
	}
	
	// extract just the filename if necessary
	tmp = strrchr(work_name, '/');
	if (tmp) strcpy(out_name, tmp+1);
	
	// remove extension(s), eg. .p8.png
	while ((tmp = strrchr(out_name, '.'))!=NULL) {
		int len = strlen(tmp);
		if (len>2 && len<=5) tmp[0] = '\0'; // 1-4 letter extension plus dot (was 1-3, extended for .doom files)
		else break;
	}
	
	// remove trailing parens (round and square)
	strcpy(work_name, out_name);
	while ((tmp=strrchr(out_name, '('))!=NULL || (tmp=strrchr(out_name, '['))!=NULL) {
		if (tmp==out_name) break;
		tmp[0] = '\0';
		tmp = out_name;
	}
	
	// make sure we haven't nuked the entire name
	if (out_name[0]=='\0') strcpy(out_name, work_name);
	
	// remove trailing whitespace
	tmp = out_name + strlen(out_name) - 1;
    while(tmp>out_name && isspace((unsigned char)*tmp)) tmp--;
    tmp[1] = '\0';
}
void getEmuName(const char* in_name, char* out_name) { // NOTE: both char arrays need to be MAX_PATH length!
	char* tmp;
	strcpy(out_name, in_name);
	tmp = out_name;
	
	// printf("--------\n  in_name: %s\n",in_name); fflush(stdout);
	
	// extract just the Roms folder name if necessary
	if (prefixMatch(ROMS_PATH, tmp)) {
		tmp += strlen(ROMS_PATH) + 1;
		char* tmp2 = strchr(tmp, '/');
		if (tmp2) tmp2[0] = '\0';
		// printf("    tmp1: %s\n", tmp);
		memmove(out_name, tmp, strlen(tmp)+1); // overlapping, strcpy isn't safe here
		tmp = out_name;
	}

	// finally extract pak name from parenths if present
	tmp = strrchr(tmp, '(');
	if (tmp) {
		tmp += 1;
		// printf("    tmp2: %s\n", tmp);
		memmove(out_name, tmp, strlen(tmp)+1); // overlapping, strcpy isn't safe here
		tmp = strchr(out_name,')');
		if (tmp) tmp[0] = '\0'; // folder names might not have a closing parenthesis
	}
	
	// printf(" out_name: %s\n", out_name); fflush(stdout);
}
void getEmuPath(char* emu_name, char* pak_path) {
	sprintf(pak_path, "%s/Emus/%s/%s.pak/launch.sh", SDCARD_PATH, PLATFORM, emu_name);
	if (exists(pak_path)) return;
	sprintf(pak_path, "%s/Emus/%s.pak/launch.sh", PAKS_PATH, emu_name);
}

// Saves must never be destroyed by a failed save: fopen(path, "w") truncates the old file before
// anything is written, so a full card, a crash or a power cut in the middle left nothing.
int writeFileAtomic(const char* path, const void* data, size_t size) {
	char tmp[MAX_PATH+8];
	if (!path || strlen(path)+5>=sizeof(tmp)) return -1;
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* f = fopen(tmp, "wb");
	if (!f) return -1;
	int ok = (size==0 || (data && fwrite(data, 1, size, f)==size));
	if (fflush(f)!=0) ok = 0;
	if (ok && fsync(fileno(f))!=0 && errno!=EINVAL && errno!=ENOTSUP && errno!=ENOSYS) ok = 0; // some filesystems can't, that's not a failed write
	if (fclose(f)!=0) ok = 0;
	if (!ok) { unlink(tmp); return -1; }
	if (rename(tmp, path)!=0) { // a filesystem that won't replace: remove the old one, the new one is complete
		if (unlink(path)!=0 || rename(tmp, path)!=0) { unlink(tmp); return -1; }
	}
	return 0;
}

// Keeping older versions of a file (saves). The copies keep the file's modification time, so
// each one says when that version was written.
int fileEquals(const char* path, const void* data, size_t size) { // 1 if the file holds exactly these bytes
	FILE* f = fopen(path, "rb");
	if (!f) return 0;
	struct stat st;
	if (fstat(fileno(f), &st)!=0 || !S_ISREG(st.st_mode) || (size_t)st.st_size!=size) { fclose(f); return 0; }
	char buf[4096];
	size_t at = 0;
	int same = 1;
	while (at<size) {
		size_t want = size-at<sizeof(buf) ? size-at : sizeof(buf);
		size_t got = fread(buf, 1, want, f);
		if (got!=want || memcmp(buf, (const char*)data+at, got)!=0) { same = 0; break; }
		at += got;
	}
	fclose(f);
	return same;
}
// file becomes dir/base.1, what was .1 becomes .2, and so on up to .keep (the oldest is dropped).
// The copy is made first: if it fails nothing has moved. Returns 0 or -1.
int backupRotate(const char* file, const char* dir, const char* base, int keep) {
	if (keep<1 || mkdirs(dir)!=0) return -1;
	char tmp[MAX_PATH+8], a[MAX_PATH+16], b[MAX_PATH+16];
	struct stat st;
	if (stat(file, &st)!=0 || !S_ISREG(st.st_mode)) return -1;
	snprintf(tmp, sizeof(tmp), "%s/%s.new", dir, base);
	FILE* in = fopen(file, "rb");
	if (!in) return -1;
	FILE* out = fopen(tmp, "wb");
	if (!out) { fclose(in); return -1; }
	char buf[8192];
	size_t got;
	int ok = 1;
	while ((got = fread(buf, 1, sizeof(buf), in))>0) if (fwrite(buf, 1, got, out)!=got) { ok = 0; break; }
	if (ferror(in)) ok = 0;
	fclose(in);
	if (fflush(out)!=0) ok = 0;
	if (ok) fsync(fileno(out));
	if (fclose(out)!=0) ok = 0;
	if (!ok) { unlink(tmp); return -1; }
	struct timeval tv[2] = { {st.st_atime, 0}, {st.st_mtime, 0} };
	utimes(tmp, tv); // best effort
	for (int n=keep-1; n>=1; n--) {
		snprintf(a, sizeof(a), "%s/%s.%i", dir, base, n);
		snprintf(b, sizeof(b), "%s/%s.%i", dir, base, n+1);
		if (rename(a, b)!=0 && errno!=ENOENT) { /* keep going: a stuck slot must not lose the new copy */ }
	}
	snprintf(a, sizeof(a), "%s/%s.1", dir, base);
	if (rename(tmp, a)!=0) { unlink(tmp); return -1; }
	return 0;
}

int mkdirs(const char* path) {
	char tmp[MAX_PATH];
	if (!path || !*path || strlen(path)>=sizeof(tmp)) return -1;
	strcpy(tmp, path);
	for (char* p=tmp+1; *p; p++) {
		if (*p!='/') continue;
		*p = '\0';
		mkdir(tmp, 0755); // fine if it's already there, a real failure shows up below
		*p = '/';
	}
	mkdir(tmp, 0755);
	struct stat st;
	return (stat(tmp, &st)==0 && S_ISDIR(st.st_mode)) ? 0 : -1;
}

void normalizeNewline(char* line) {
	int len = strlen(line);
	if (len>1 && line[len-1]=='\n' && line[len-2]=='\r') { // windows!
		line[len-2] = '\n';
		line[len-1] = '\0';
	}
}
void trimTrailingNewlines(char* line) {
	int len = strlen(line);
	while (len>0 && line[len-1]=='\n') {
		line[len-1] = '\0'; // trim newline
		len -= 1;
	}
}
void trimSortingMeta(char** str) { // eg. `001) `
	// TODO: this code is suss
	char* safe = *str;
	while(isdigit(**str)) *str += 1; // ignore leading numbers

	if (*str[0]==')') { // then match a closing parenthesis
		*str += 1;
	}
	else { //  or bail, restoring the string to its original value
		*str = safe;
		return;
	}
	
	while(isblank(**str)) *str += 1; // ignore leading space
}

///////////////////////////////////////

int exists(char* path) {
	return access(path, F_OK)==0;
}
void touch(char* path) {
	close(open(path, O_RDWR|O_CREAT, 0777));
}
void putFile(char* path, char* contents) {
	struct stat st;
	int kernel_file = !strncmp(path, "/sys/", 5) || !strncmp(path, "/proc/", 6) || !strncmp(path, "/dev/", 5); // sysfs files look like regular files to stat()
	if (kernel_file || (stat(path, &st)==0 && !S_ISREG(st.st_mode))) { // no temp file can be made next to these
		FILE* f = fopen(path, "w");
		if (f) { fputs(contents, f); fclose(f); }
		return;
	}
	writeFileAtomic(path, contents, strlen(contents)); // not fopen "w": that empties the old file first
}
void getFile(char* path, char* buffer, size_t buffer_size) {
	if (buffer_size==0) return;
	buffer[0] = '\0'; // a missing or unreadable file reads as empty, never as whatever was in the buffer
	FILE *file = fopen(path, "r");
	if (file) {
		fseek(file, 0L, SEEK_END);
		long end = ftell(file); // -1 (or huge for a directory) if this isn't a regular file
		size_t size = end<0 ? 0 : (size_t)end;
		if (size>buffer_size-1) size = buffer_size - 1;
		rewind(file);
		size_t got = fread(buffer, sizeof(char), size, file);
		fclose(file);
		buffer[got] = '\0';
	}
}
char* allocFile(char* path) { // caller must free!
	char* contents = NULL;
	FILE *file = fopen(path, "r");
	if (file) {
		fseek(file, 0L, SEEK_END);
		long end = ftell(file); // -1 if this isn't a regular file
		size_t size = end<0 ? 0 : end;
		contents = calloc(size+1, sizeof(char));
		if (contents) {
			fseek(file, 0L, SEEK_SET);
			size = fread(contents, sizeof(char), size, file);
			contents[size] = '\0';
		}
		fclose(file);
	}
	return contents;
}
int getInt(char* path) {
	int i = 0;
	FILE *file = fopen(path, "r");
	if (file!=NULL) {
		fscanf(file, "%i", &i);
		fclose(file);
	}
	return i;
}
void putInt(char* path, int value) {
	char buffer[8];
	sprintf(buffer, "%d", value);
	putFile(path, buffer);
}

uint64_t getMicroseconds(void) {
    uint64_t ret;
    struct timeval tv;

    gettimeofday(&tv, NULL);

    ret = (uint64_t)tv.tv_sec * 1000000;
    ret += (uint64_t)tv.tv_usec;

    return ret;
}

// Play time (see utils.h)
int Playtime_parse(char* text, PlayRecord* out, int max) {
	int count = 0;
	char* save = NULL;
	for (char* line = strtok_r(text, "\n", &save); line && count<max; line = strtok_r(NULL, "\n", &save)) {
		char* t1 = strchr(line, '\t');
		if (!t1) continue;
		char* t2 = strchr(t1+1, '\t');
		if (!t2) continue;
		char* t3 = strchr(t2+1, '\t');
		if (!t3) continue;
		*t1 = *t2 = *t3 = '\0';
		char* path = t3 + 1;
		int len = strlen(path);
		while (len>0 && (path[len-1]=='\r' || path[len-1]==' ')) path[--len] = '\0';
		if (!len || len>=(int)sizeof(out[count].path)) continue;
		long seconds = atol(line);
		if (seconds<0) seconds = 0;
		out[count].seconds = seconds;
		out[count].launches = atoi(t1+1);
		out[count].last = atol(t2+1);
		strcpy(out[count].path, path);
		count++;
	}
	return count;
}
int Playtime_add(PlayRecord* records, int count, int max, const char* path, long seconds, int launches, long now) {
	if (!path || !*path || strlen(path)>=sizeof(records[0].path) || strchr(path, '\t') || strchr(path, '\n')) return count;
	int i;
	for (i=0; i<count; i++) if (!strcmp(records[i].path, path)) break;
	if (i==count) {
		if (count>=max) return count;
		memset(&records[i], 0, sizeof(PlayRecord));
		strcpy(records[i].path, path);
		count++;
	}
	if (seconds>0) records[i].seconds += seconds;
	if (launches>0) records[i].launches += launches;
	if (now>records[i].last) records[i].last = now;
	return count;
}
int Playtime_format(const PlayRecord* records, int count, char* out, int size) {
	int len = 0;
	out[0] = '\0';
	for (int i=0; i<count; i++) {
		int n = snprintf(out + len, size - len, "%ld\t%i\t%ld\t%s\n", records[i].seconds, records[i].launches, records[i].last, records[i].path);
		if (n<0 || n>=size - len) break;
		len += n;
	}
	return len;
}
void Playtime_name(const char* path, char* out, int size) {
	const char* base = strrchr(path, '/');
	base = base ? base+1 : path;
	snprintf(out, size, "%s", base);
	char* dot = strrchr(out, '.');
	if (dot && strlen(dot)>1 && strlen(dot)<=5) { // .nes, .gba, .chd (but not the dot in "Mr. Do!")
		int ext = 1;
		for (char* p=dot+1; *p; p++) if (!isalnum((unsigned char)*p)) ext = 0;
		if (ext) *dot = '\0';
	}
	for (;;) { // trailing (USA) [!] style tags
		int len = strlen(out);
		while (len>0 && out[len-1]==' ') out[--len] = '\0';
		if (len>1 && (out[len-1]==')' || out[len-1]==']')) {
			char open = out[len-1]==')' ? '(' : '[';
			char* p = strrchr(out, open);
			if (p && p>out) { *p = '\0'; continue; }
		}
		break;
	}
}
void Playtime_duration(long seconds, char* out, int size) {
	if (seconds<60) snprintf(out, size, "<1 min");
	else if (seconds<3600) snprintf(out, size, "%ld min", seconds/60);
	else snprintf(out, size, "%ldh %02ldm", seconds/3600, (seconds%3600)/60);
}
void Playtime_save(const char* file, const char* path, long seconds, int launches, long now) {
	PlayRecord* records = calloc(PLAYTIME_MAX, sizeof(PlayRecord));
	char* text = calloc(1, PLAYTIME_MAX * 600);
	if (!records || !text) { free(records); free(text); return; }
	int count = 0;
	FILE* f = fopen(file, "r");
	if (f) {
		size_t n = fread(text, 1, PLAYTIME_MAX*600 - 1, f);
		text[n] = '\0';
		fclose(f);
		count = Playtime_parse(text, records, PLAYTIME_MAX);
	}
	count = Playtime_add(records, count, PLAYTIME_MAX, path, seconds, launches, now);
	Playtime_format(records, count, text, PLAYTIME_MAX * 600);
	putFile((char*)file, text);
	free(records);
	free(text);
}
// end of the play time code

// Cheat database (see utils.h)
static const struct { const char* tag; const char* folder; } cheatdb_folders[] = {
	{"FC", "Nintendo - Nintendo Entertainment System"}, {"NES", "Nintendo - Nintendo Entertainment System"},
	{"FDS", "Nintendo - Family Computer Disk System"},
	{"GB", "Nintendo - Game Boy"}, {"SGB", "Nintendo - Game Boy"},
	{"GBC", "Nintendo - Game Boy Color"}, {"GBA", "Nintendo - Game Boy Advance"},
	{"SFC", "Nintendo - Super Nintendo Entertainment System"}, {"SNES", "Nintendo - Super Nintendo Entertainment System"},
	{"SUPER", "Nintendo - Super Nintendo Entertainment System"}, {"SUPA", "Nintendo - Super Nintendo Entertainment System"},
	{"SNX", "Nintendo - Super Nintendo Entertainment System"},
	{"VB", "Nintendo - Virtual Boy"}, {"N64", "Nintendo - Nintendo 64"}, {"NDS", "Nintendo - Nintendo DS"},
	{"MD", "Sega - Mega Drive - Genesis"}, {"GEN", "Sega - Mega Drive - Genesis"}, {"GENESIS", "Sega - Mega Drive - Genesis"},
	{"SMS", "Sega - Master System - Mark III"}, {"GG", "Sega - Game Gear"}, {"SG1000", "Sega - SG-1000"},
	{"32X", "Sega - 32X"}, {"SEGACD", "Sega - Mega-CD - Sega CD"}, {"SCD", "Sega - Mega-CD - Sega CD"},
	{"PS", "Sony - PlayStation"}, {"PSX", "Sony - PlayStation"},
	{"PCE", "NEC - PC Engine - TurboGrafx 16"}, {"TG16", "NEC - PC Engine - TurboGrafx 16"},
	{"NGP", "SNK - Neo Geo Pocket"}, {"NGPC", "SNK - Neo Geo Pocket Color"},
	{"WS", "Bandai - WonderSwan"}, {"WSC", "Bandai - WonderSwan Color"},
	{"LYNX", "Atari - Lynx"}, {"A2600", "Atari - 2600"}, {"A7800", "Atari - 7800"},
	{NULL, NULL}
};
const char* Cheatdb_folder(const char* tag) {
	for (int i=0; cheatdb_folders[i].tag; i++) {
		if (!strcasecmp(cheatdb_folders[i].tag, tag)) return cheatdb_folders[i].folder;
	}
	return NULL;
}
void Cheatdb_encode(const char* in, char* out, int size) {
	int n = 0;
	for (const unsigned char* p = (const unsigned char*)in; *p && n+4<size; p++) {
		if (isalnum(*p) || *p=='-' || *p=='_' || *p=='.' || *p=='~') out[n++] = *p;
		else n += sprintf(out+n, "%%%02X", *p);
	}
	out[n] = '\0';
}
static int Cheatdb_hex(char c) { return isdigit((unsigned char)c) ? c-'0' : (c>='a' && c<='f') ? c-'a'+10 : (c>='A' && c<='F') ? c-'A'+10 : -1; }
int Cheatdb_names(const char* json, char* out, int size) {
	int n = 0, count = 0;
	const char* p = json;
	out[0] = '\0';
	while ((p = strstr(p, "\"path\""))) {
		p += 6;
		while (*p==' ' || *p==':' || *p=='\t' || *p=='\n') p++;
		if (*p!='"') continue;
		p++;
		char name[512];
		int len = 0;
		while (*p && *p!='"' && len<(int)sizeof(name)-5) {
			if (*p=='\\' && p[1]) {
				p++;
				if (*p=='u' && Cheatdb_hex(p[1])>=0 && Cheatdb_hex(p[2])>=0 && Cheatdb_hex(p[3])>=0 && Cheatdb_hex(p[4])>=0) {
					int u = (Cheatdb_hex(p[1])<<12) | (Cheatdb_hex(p[2])<<8) | (Cheatdb_hex(p[3])<<4) | Cheatdb_hex(p[4]);
					p += 4;
					if (u<0x80) name[len++] = u;
					else if (u<0x800) { name[len++] = 0xC0 | (u>>6); name[len++] = 0x80 | (u&0x3F); }
					else { name[len++] = 0xE0 | (u>>12); name[len++] = 0x80 | ((u>>6)&0x3F); name[len++] = 0x80 | (u&0x3F); }
				}
				else if (*p=='n' || *p=='t') name[len++] = ' ';
				else name[len++] = *p; // \" \\ \/
				p++;
			}
			else name[len++] = *p++;
		}
		name[len] = '\0';
		if (*p=='"') p++;
		if (len<5 || strcasecmp(name+len-4, ".cht") || strchr(name, '/')) continue; // files in this folder only
		if (n + len + 2 >= size) break;
		memcpy(out+n, name, len);
		n += len;
		out[n++] = '\n';
		out[n] = '\0';
		count++;
	}
	return count;
}
// "Legend of Zelda, The (USA) (Rev 1).nes" -> "thelegendofzelda": the title only, lowercase letters and digits
static void Cheatdb_key(const char* name, char* out, int size) {
	char title[512];
	snprintf(title, sizeof(title), "%s", name);
	char* cut = strstr(title, " (");
	char* cut2 = strstr(title, " [");
	if (!cut || (cut2 && cut2<cut)) cut = cut2;
	if (cut) *cut = '\0';
	else { // no tags: drop the extension (a dot and 1 to 4 letters or digits at the end)
		char* dot = strrchr(title, '.');
		if (dot && strlen(dot+1)>=1 && strlen(dot+1)<=4) {
			int ok = 1;
			for (char* q = dot+1; *q; q++) if (!isalnum((unsigned char)*q)) ok = 0;
			if (ok) *dot = '\0';
		}
	}
	int len = strlen(title);
	int the = 0;
	if (len>5 && !strcasecmp(title+len-5, ", The")) { title[len-5] = '\0'; the = 1; }
	int n = 0;
	if (the) n += snprintf(out, size, "the");
	for (const char* p = title; *p && n<size-1; p++) {
		if (isalnum((unsigned char)*p)) out[n++] = tolower((unsigned char)*p);
	}
	out[n] = '\0';
}
static int Cheatdb_has(const char* name, const char* tag) { // is "(...tag...)" among the name's parenthesized parts?
	for (const char* p = strchr(name, '('); p; p = strchr(p+1, '(')) {
		const char* end = strchr(p, ')');
		if (!end) break;
		for (const char* q = p+1; q+strlen(tag)<=end; q++) {
			if (!strncmp(q, tag, strlen(tag))) return 1;
		}
	}
	return 0;
}
int Cheatdb_pick(const char* names, const char* game, char* out, int size) {
	char gamekey[256], key[256];
	Cheatdb_key(game, gamekey, sizeof(gamekey));
	if (!gamekey[0]) return 0;
	char want[512];
	snprintf(want, sizeof(want), "%s", game);
	char* dot = strrchr(want, '.');
	if (dot && strlen(dot+1)>=1 && strlen(dot+1)<=4) *dot = '\0';
	int best = -1;
	char best_name[512] = "";
	const char* p = names;
	while (*p) {
		const char* eol = strchr(p, '\n');
		int len = eol ? (int)(eol-p) : (int)strlen(p);
		char name[512];
		if (len>0 && len<(int)sizeof(name)) {
			memcpy(name, p, len);
			name[len] = '\0';
			char base[512];
			snprintf(base, sizeof(base), "%s", name);
			if (len>4) base[len-4] = '\0'; // the .cht
			Cheatdb_key(base, key, sizeof(key));
			if (!strcmp(key, gamekey)) {
				int score = 100;
				if (!strcmp(base, want)) score += 1000;
				static const char* regions[] = {"USA", "Europe", "Japan", "World", NULL};
				for (int i=0; regions[i]; i++) {
					if (Cheatdb_has(base, regions[i]) && Cheatdb_has(want, regions[i])) score += 20;
				}
				if (Cheatdb_has(base, "World")) score += 10;
				else if (Cheatdb_has(base, "USA")) score += 8;
				else if (Cheatdb_has(base, "Europe")) score += 6;
				static const char* odd[] = {"Beta", "Proto", "Demo", "Sample", "Unl", "Pirate", "Hack", "Kiosk", NULL};
				for (int i=0; odd[i]; i++) {
					if (Cheatdb_has(base, odd[i]) && !Cheatdb_has(want, odd[i])) score -= 30;
				}
				for (const char* q = strchr(base, '('); q; q = strchr(q+1, '(')) score -= 2; // fewer extra tags first (Rev, Game Genie, Action Replay...)
				if (score>best) { best = score; snprintf(best_name, sizeof(best_name), "%s", name); }
			}
		}
		if (!eol) break;
		p = eol + 1;
	}
	if (best<0) return 0;
	snprintf(out, size, "%s", best_name);
	return 1;
}
// end of the cheat database code

// Recently played (see utils.h)
size_t Recents_bump(const char* text, const char* rel_path, int max, char* out, size_t size) {
	if (size==0) return 0;
	out[0] = '\0';
	size_t n = 0;
	size_t path_len = strlen(rel_path);
	const char* mine = NULL;      // the existing line for this game, kept with its alias
	size_t mine_len = 0;
	for (const char* p = text ? text : ""; *p; ) {
		const char* eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);
		size_t trimmed = len;
		while (trimmed>0 && (p[trimmed-1]=='\r')) trimmed--;
		if (trimmed>0 && !mine && trimmed>=path_len && !strncmp(p, rel_path, path_len) && (trimmed==path_len || p[path_len]=='\t')) { mine = p; mine_len = trimmed; }
		p += len + (eol ? 1 : 0);
	}
	int lines = 0;
	if (max>0) { // the game first
		n += snprintf(out+n, size-n, "%.*s\n", (int)(mine ? mine_len : path_len), mine ? mine : rel_path);
		lines = 1;
	}
	for (const char* p = text ? text : ""; *p && lines<max && n<size; ) {
		const char* eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);
		size_t trimmed = len;
		while (trimmed>0 && p[trimmed-1]=='\r') trimmed--;
		if (trimmed>0 && p!=mine) {
			int same = trimmed>=path_len && !strncmp(p, rel_path, path_len) && (trimmed==path_len || p[path_len]=='\t');
			if (!same) {
				n += snprintf(out+n, size-n, "%.*s\n", (int)trimmed, p);
				lines++;
			}
		}
		p += len + (eol ? 1 : 0);
	}
	if (n>=size) { n = size-1; out[n] = '\0'; }
	return n;
}
void Recents_add(const char* sd_path) {
	size_t prefix = strlen(SDCARD_PATH);
	if (strncmp(sd_path, SDCARD_PATH, prefix)) return; // the list is platform agnostic: paths from the card's root
	char* old = allocFile(RECENT_PATH); // NULL if there is none yet
	size_t cap = (old ? strlen(old) : 0) + strlen(sd_path) + 16;
	char* out = malloc(cap);
	if (out) {
		size_t n = Recents_bump(old, sd_path + prefix, 24, out, cap);
		mkdirs(SHARED_USERDATA_PATH "/.minui");
		writeFileAtomic(RECENT_PATH, out, n);
		free(out);
	}
	free(old);
}
// end of the recently played code

// HTTP date (see utils.h)
time_t HTTPDate_parse(const char* headers) {
	if (!headers) return 0;
	static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
	for (const char* line = headers; *line; ) {
		const char* eol = strchr(line, '\n');
		size_t len = eol ? (size_t)(eol - line) : strlen(line);
		if (len>5 && !strncasecmp(line, "date:", 5)) {
			char text[128];
			snprintf(text, sizeof(text), "%.*s", len<sizeof(text)-1 ? (int)len : (int)sizeof(text)-1, line+5);
			int day, year, hour, minute, second;
			char month[8];
			const char* p = text;
			while (*p==' ' || *p=='\t') p++;
			const char* comma = strchr(p, ',');
			if (comma) p = comma + 1; // the weekday
			if (sscanf(p, " %2d %3s %4d %2d:%2d:%2d", &day, month, &year, &hour, &minute, &second)==6) {
				int mon = -1;
				for (int i=0; i<12; i++) if (!strncasecmp(month, months[i], 3)) mon = i;
				if (mon>=0 && day>=1 && day<=31 && hour>=0 && hour<=23 && minute>=0 && minute<=59 && second>=0 && second<=60 && year>=2024 && year<=2100) {
					struct tm tmv;
					memset(&tmv, 0, sizeof(tmv));
					tmv.tm_year = year - 1900;
					tmv.tm_mon = mon;
					tmv.tm_mday = day;
					tmv.tm_hour = hour;
					tmv.tm_min = minute;
					tmv.tm_sec = second==60 ? 59 : second;
					return timegm(&tmv);
				}
			}
			return 0; // the first Date line is the one; a bad one means no date
		}
		line += len + (eol ? 1 : 0);
	}
	return 0;
}
// end of the HTTP date code

// Random game (see utils.h)
int Random_pick(const unsigned char* played, int count, uint32_t* state) {
	if (count<=0 || !state) return -1;
	if (*state==0) *state = 0x9E3779B9u;
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	int fresh = 0;
	for (int i=0; i<count; i++) if (!played || !played[i]) fresh++;
	int use_fresh = fresh>0;
	int n = use_fresh ? fresh : count;
	int pick = (int)((x >> 8) % (uint32_t)n); // the low bits of xorshift are the weakest
	for (int i=0; i<count; i++) {
		if (use_fresh && played && played[i]) continue;
		if (pick--==0) return i;
	}
	return count-1; // not reached
}
// end of the random game code
