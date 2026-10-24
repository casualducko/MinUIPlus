#define _GNU_SOURCE // for strcasestr
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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