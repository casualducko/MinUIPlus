// GFX_truncateText() and GFX_truncateUniqueText() from api.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct TTF_Font TTF_Font;
// every code point is 10px wide, enough to exercise the width logic
static int TTF_SizeUTF8(TTF_Font* f, const char* t, int* w, int* h) {
	int n=0; for (const unsigned char* p=(const unsigned char*)t; *p; p++) if ((*p&0xC0)!=0x80) n++;
	if (w) *w = n*10; return 0;
}
static int valid_utf8(const char* s) {
	const unsigned char* p=(const unsigned char*)s;
	while (*p) {
		int n = *p<0x80?0 : (*p&0xE0)==0xC0?1 : (*p&0xF0)==0xE0?2 : (*p&0xF8)==0xF0?3 : -1;
		if (n<0) return 0; p++;
		while (n--) { if ((*p&0xC0)!=0x80) return 0; p++; }
	}
	return 1;
}
#include "gen/GFX_truncate.inc"

static int fails = 0;
#define CHECK(c,m) do { int ok=(c); printf("%s  %s\n", ok?"ok  ":"FAIL", m); if(!ok) fails++; } while(0)
int main(void) {
	char name_out[256], unique_out[256]; int w;

	w = GFX_truncateUniqueText(NULL,"Short","Short (GBC)",name_out,unique_out,1000,20);
	CHECK(!strcmp(name_out,"Short") && !strcmp(unique_out,"Short (GBC)"), "fits: unchanged");

	const char* name = "A Very Long Game Name That Does Not Fit";
	char unique[256]; snprintf(unique,256,"%s (GBC)",name);
	w = GFX_truncateUniqueText(NULL,name,unique,name_out,unique_out,300,20);
	printf("      -> \"%s\" (width %d of 300)\n", unique_out, w);
	CHECK(w<=300, "long name: fits the available width");
	CHECK(strlen(unique_out)>6 && !strcmp(unique_out+strlen(unique_out)-6," (GBC)"), "long name: the alt tag survives");
	CHECK(!strncmp(unique_out,name_out,strlen(name_out)) && strstr(name_out,"..."), "long name: the name is what gets truncated");

	// the alt tag alone is wider than the room: must terminate
	GFX_truncateUniqueText(NULL,"Game","Game (A very long tag indeed)",name_out,unique_out,60,20);
	CHECK(1, "alt tag wider than the room: terminates");

	// these wrote before the start of the buffer
	GFX_truncateText(NULL,"GB",name_out,5,20);
	GFX_truncateText(NULL,"",name_out,5,20);
	GFX_truncateText(NULL,"ab",name_out,0,0);
	CHECK(1, "short strings in a tiny width: no underflow");

	GFX_truncateUniqueText(NULL,"Alias","Real Filename.gb",name_out,unique_out,1000,20);
	CHECK(!strcmp(name_out,"Alias") && !strcmp(unique_out,"Real Filename.gb"), "unrelated unique name (an alias): truncated separately");

	const char* u = "Pok\xc3\xa9mon Pok\xc3\xa9mon Pok\xc3\xa9mon Pok\xc3\xa9mon \xe3\x83\x9d\xe3\x82\xb1\xe3\x83\xa2\xe3\x83\xb3";
	int valid = 1;
	for (int mw=40; mw<=300; mw+=7) { GFX_truncateText(NULL,u,name_out,mw,20); valid &= valid_utf8(name_out); }
	CHECK(valid, "UTF-8 is never cut in half");

	char big[1200]; memset(big,'x',1199); big[1199]=0;
	GFX_truncateText(NULL,big,name_out,100000,0);
	CHECK(strlen(name_out)==255, "very long input is bounded to 255 bytes");
	return fails!=0;
}
