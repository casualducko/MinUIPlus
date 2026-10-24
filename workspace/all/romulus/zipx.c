#include "zipx.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>
#include <zlib.h>

static uint16_t rd16(const uint8_t* p) { return p[0] | p[1]<<8; }
static uint32_t rd32(const uint8_t* p) { return p[0] | p[1]<<8 | p[2]<<16 | (uint32_t)p[3]<<24; }

static int ext_matches(const char* name, const char* const* exts) {
	const char* dot = strrchr(name, '.');
	if (!dot || !dot[1]) return 0;
	for (int i=0; exts[i]; i++) {
		if (!strcasecmp(dot+1, exts[i])) return 1;
	}
	return 0;
}

int zipx_find(const char* zip_path, const char* const* exts, ZipEntry* out) {
	FILE* f = fopen(zip_path, "rb");
	if (!f) return ZIPX_E_OPEN;
	int result = ZIPX_E_NOTZIP;
	uint8_t* cd = NULL;
	
	if (fseek(f, 0, SEEK_END)) goto done;
	long size = ftell(f);
	if (size<22) goto done;
	long tail = size<(65535+22) ? size : (65535+22);
	uint8_t* buf = malloc(tail);
	if (!buf) { result = ZIPX_E_OPEN; goto done; }
	fseek(f, size-tail, SEEK_SET);
	if (fread(buf, 1, tail, f)!=(size_t)tail) { free(buf); result = ZIPX_E_OPEN; goto done; }
	long e = -1;
	for (long i=tail-22; i>=0; i--) {
		if (rd32(buf+i)==0x06054b50) { e = i; break; }
	}
	if (e<0) { free(buf); goto done; }
	unsigned entries = rd16(buf+e+10);
	uint32_t cd_size = rd32(buf+e+12), cd_off = rd32(buf+e+16);
	free(buf);
	if (entries==0xFFFF || cd_size==0xFFFFFFFFu || cd_off==0xFFFFFFFFu) { result = ZIPX_E_UNSUPPORTED; goto done; } // zip64
	if ((long)cd_off+(long)cd_size>size) { result = ZIPX_E_CORRUPT; goto done; }
	
	cd = malloc(cd_size ? cd_size : 1);
	if (!cd) { result = ZIPX_E_OPEN; goto done; }
	fseek(f, cd_off, SEEK_SET);
	if (fread(cd, 1, cd_size, f)!=cd_size) { result = ZIPX_E_CORRUPT; goto done; }
	
	result = ZIPX_E_NONE;
	uint32_t p = 0;
	for (unsigned i=0; i<entries; i++) {
		if (p+46>cd_size || rd32(cd+p)!=0x02014b50) { result = ZIPX_E_CORRUPT; goto done; }
		unsigned flags = rd16(cd+p+8), method = rd16(cd+p+10);
		uint32_t crc = rd32(cd+p+16), csize = rd32(cd+p+20), usize = rd32(cd+p+24);
		unsigned nlen = rd16(cd+p+28), xlen = rd16(cd+p+30), clen = rd16(cd+p+32);
		uint32_t off = rd32(cd+p+42);
		if ((uint64_t)p+46+nlen+xlen+clen>cd_size) { result = ZIPX_E_CORRUPT; goto done; }
		
		char name[512];
		unsigned n = nlen<sizeof(name)-1 ? nlen : sizeof(name)-1;
		memcpy(name, cd+p+46, n);
		name[n] = '\0';
		p += 46+nlen+xlen+clen;
		
		if (n && (name[n-1]=='/' || name[n-1]=='\\')) continue; // folder
		char* base = name;
		for (char* s=name; *s; s++) if (*s=='/' || *s=='\\') base = s+1;
		if (!*base || !strcmp(base, ".") || !strcmp(base, "..")) continue;
		if (!ext_matches(base, exts)) continue;
		
		if (flags & 1) { result = ZIPX_E_UNSUPPORTED; continue; } // encrypted, maybe another entry is fine
		if (method!=0 && method!=8) { result = ZIPX_E_UNSUPPORTED; continue; }
		if (csize==0xFFFFFFFFu || usize==0xFFFFFFFFu || usize>ZIPX_MAX_SIZE) { result = ZIPX_E_UNSUPPORTED; continue; }
		
		memset(out, 0, sizeof(*out));
		snprintf(out->name, sizeof(out->name), "%s", base);
		out->csize = csize; out->usize = usize; out->crc = crc; out->offset = off; out->method = method;
		result = ZIPX_OK;
		goto done;
	}
done:
	free(cd);
	fclose(f);
	return result;
}

int zipx_extract(const char* zip_path, const ZipEntry* e, const char* out_path) {
	FILE* f = fopen(zip_path, "rb");
	if (!f) return ZIPX_E_OPEN;
	FILE* o = NULL;
	int result = ZIPX_E_CORRUPT;
	z_stream zs;
	int zinit = 0;
	
	uint8_t lh[30];
	if (fseek(f, e->offset, SEEK_SET) || fread(lh, 1, 30, f)!=30 || rd32(lh)!=0x04034b50) goto done;
	if (fseek(f, rd16(lh+26)+rd16(lh+28), SEEK_CUR)) goto done;
	
	o = fopen(out_path, "wb");
	if (!o) { result = ZIPX_E_WRITE; goto done; }
	
	static uint8_t in[64*1024], out[64*1024];
	uint32_t crc = crc32(0, Z_NULL, 0);
	uint64_t total = 0;
	uint32_t left = e->csize;
	
	if (e->method==0) {
		if (e->csize!=e->usize) goto done;
		while (left>0) {
			size_t want = left<sizeof(in) ? left : sizeof(in);
			if (fread(in, 1, want, f)!=want) goto done;
			if (fwrite(in, 1, want, o)!=want) { result = ZIPX_E_WRITE; goto done; }
			crc = crc32(crc, in, want);
			total += want;
			left -= want;
		}
	}
	else {
		memset(&zs, 0, sizeof(zs));
		if (inflateInit2(&zs, -15)!=Z_OK) goto done;
		zinit = 1;
		int zr = Z_OK;
		while (zr!=Z_STREAM_END) {
			if (zs.avail_in==0) {
				if (left==0) goto done; // ran out of input before the end of the stream
				size_t want = left<sizeof(in) ? left : sizeof(in);
				if (fread(in, 1, want, f)!=want) goto done;
				left -= want;
				zs.next_in = in;
				zs.avail_in = want;
			}
			zs.next_out = out;
			zs.avail_out = sizeof(out);
			zr = inflate(&zs, Z_NO_FLUSH);
			if (zr!=Z_OK && zr!=Z_STREAM_END) goto done;
			size_t got = sizeof(out) - zs.avail_out;
			total += got;
			if (total>e->usize) goto done; // more than the entry declared
			if (got) {
				if (fwrite(out, 1, got, o)!=got) { result = ZIPX_E_WRITE; goto done; }
				crc = crc32(crc, out, got);
			}
		}
	}
	if (total!=e->usize || crc!=e->crc) goto done;
	result = ZIPX_OK;
done:
	if (zinit) inflateEnd(&zs);
	if (o && fclose(o)!=0 && result==ZIPX_OK) result = ZIPX_E_WRITE;
	fclose(f);
	if (result!=ZIPX_OK && o) remove(out_path); // never leave a partial ROM behind
	return result;
}
