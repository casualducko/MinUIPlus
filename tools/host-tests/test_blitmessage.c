// GFX_blitMessage() from api.c: long rows, many rows, a row that fails to render
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { int w, h; } SDL_Surface;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct TTF_Font TTF_Font;
typedef struct { int r,g,b; } SDL_Color;
#define COLOR_WHITE ((SDL_Color){255,255,255})
#define SCALE1(a) ((a)*2)
static int blits;
static SDL_Surface* TTF_RenderUTF8_Blended(TTF_Font* f, const char* t, SDL_Color c) {
	if (t[0]=='!') return NULL; // a render failure
	SDL_Surface* s = malloc(sizeof(SDL_Surface)); s->w = strlen(t)*4; s->h = 20; return s;
}
static int SDL_BlitSurface(SDL_Surface* a, void* b, SDL_Surface* d, SDL_Rect* r) { blits++; return 0; }
static void SDL_FreeSurface(SDL_Surface* s) { free(s); }
static SDL_Surface* GFX_renderText(TTF_Font* f, const char* t, SDL_Color c) { return TTF_RenderUTF8_Blended(f, t, c); }
#include "gen/GFX_utf8Trim.inc"
#include "gen/GFX_blitMessage.inc"

static int fails = 0;
#define CHECK(c,m) do { int ok=(c); printf("%s  %s (%d rows drawn)\n", ok?"ok  ":"FAIL", m, blits); if(!ok) fails++; } while(0)
int main(void) {
	SDL_Surface dst = {640,480};
	char* longline = malloc(1000); memset(longline,'a',999); longline[999]=0;
	blits = 0; GFX_blitMessage(NULL, longline, &dst, NULL);
	CHECK(blits==1, "a 1000 character row (overflowed a 256 byte buffer)");

	char many[200]; many[0]=0; for (int i=0;i<20;i++) strcat(many,"row\n");
	blits = 0; GFX_blitMessage(NULL, many, &dst, NULL);
	CHECK(blits==16, "20 rows: the ones that fit are drawn (was: nothing)");

	blits = 0; GFX_blitMessage(NULL, "good\n!bad\ngood", &dst, NULL);
	CHECK(blits==2, "a row that fails to render is skipped (was: NULL dereference)");
	return fails!=0;
}
