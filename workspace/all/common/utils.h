#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

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
int writeFileAtomic(const char* path, const void* data, size_t size); // writes path.tmp, syncs, renames over path: the old file survives any failure; 0 or -1
int mkdirs(const char* path); // mkdir -p without a shell (a folder name is never a command); 0 if it exists afterwards, -1 if not
int fileEquals(const char* path, const void* data, size_t size); // 1 if the file holds exactly these bytes
int backupRotate(const char* file, const char* dir, const char* base, int keep); // file -> dir/base.1, .1 -> .2 ... (keeps the file's mtime); 0 or -1
void touch(char* path);
void putFile(char* path, char* contents);
char* allocFile(char* path); // caller must free
void getFile(char* path, char* buffer, size_t buffer_size);
void putInt(char* path, int value);
int getInt(char* path);

uint64_t getMicroseconds(void);

#endif
