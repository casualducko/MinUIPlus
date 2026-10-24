// Pull one ROM out of a .zip. Handles stored and deflated entries (zip64 and encryption
// are refused), checks the CRC, and never trusts the entry's path (only its base name is used).
#ifndef ROMULUS_ZIPX_H
#define ROMULUS_ZIPX_H
#include <stdint.h>

#define ZIPX_MAX_SIZE (256u*1024*1024) // refuse entries that claim to be bigger than this

typedef struct {
	char name[512];		// base name of the entry
	uint32_t csize, usize, crc, offset;
	int method;
} ZipEntry;

enum { ZIPX_OK=0, ZIPX_E_OPEN=-1, ZIPX_E_NOTZIP=-2, ZIPX_E_NONE=-3, ZIPX_E_UNSUPPORTED=-4, ZIPX_E_CORRUPT=-5, ZIPX_E_WRITE=-6 };

// first entry whose extension is in exts (NULL-terminated, lowercase, no dot)
int zipx_find(const char* zip_path, const char* const* exts, ZipEntry* out);
int zipx_extract(const char* zip_path, const ZipEntry* e, const char* out_path);
#endif
