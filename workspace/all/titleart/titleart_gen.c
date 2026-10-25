// Title art generator: makes the picture for one game (see titleart.c, titleart_core.c). It is shared
// by the Title Art tool, the Romulus tool (a game that was just downloaded gets its picture at once)
// and the boot pass (titleart.elf --auto).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "titleart_core.h"
#include "titleart_gen.h"

#define TILE 360
#define CELL_W 6
#define CELL_H 12
#define BIG_PX 96             // the title is drawn this big, then shrunk to the letter grid
#define CELL_PX 11            // the font size of the characters in the tile

// the tile

typedef struct { char text[TA_LINE_MAX]; int w, h; uint8_t* a; } Raster;
static TTF_Font* big_font = NULL;
static TTF_Font* cell_font = NULL;
static Raster rasters[12];
static int raster_next = 0;

static Raster* rasterLine(const char* text) { // the line in big letters: its coverage, cropped
	for (int i=0; i<12; i++) if (rasters[i].a && !strcmp(rasters[i].text, text)) return &rasters[i];
	SDL_Color white = {255,255,255,0};
	SDL_Surface* s = TTF_RenderUTF8_Blended(big_font, text, white);
	if (!s) return NULL;
	Uint32 amask = s->format->Amask, ashift = s->format->Ashift;
	int x0 = s->w, y0 = s->h, x1 = -1, y1 = -1;
	for (int y=0; y<s->h; y++) {
		Uint32* row = (Uint32*)((Uint8*)s->pixels + y*s->pitch);
		for (int x=0; x<s->w; x++) {
			if ((row[x] & amask) >> ashift) {
				if (x<x0) x0 = x;
				if (x>x1) x1 = x;
				if (y<y0) y0 = y;
				if (y>y1) y1 = y;
			}
		}
	}
	if (x1<0) { SDL_FreeSurface(s); return NULL; }
	Raster* r = &rasters[raster_next];
	raster_next = (raster_next + 1) % 12;
	free(r->a);
	r->w = x1 - x0 + 1;
	r->h = y1 - y0 + 1;
	r->a = malloc((size_t)r->w * r->h);
	snprintf(r->text, sizeof(r->text), "%s", text);
	if (r->a) {
		for (int y=0; y<r->h; y++) {
			Uint32* row = (Uint32*)((Uint8*)s->pixels + (y0+y)*s->pitch);
			for (int x=0; x<r->w; x++) r->a[y*r->w + x] = (row[x0+x] & amask) >> ashift;
		}
	}
	SDL_FreeSurface(s);
	return r->a ? r : NULL;
}
static float lineAspect(const char* text, void* ctx) {
	(void)ctx;
	Raster* r = rasterLine(text);
	return r ? (float)r->w / r->h : 0;
}
static void shrink(const Raster* r, int dw, int dh, uint8_t* out) { // box filter
	for (int y=0; y<dh; y++) {
		int y0 = (int)floorf(y * (float)r->h / dh);
		int y1 = (int)ceilf((y+1) * (float)r->h / dh);
		if (y1<=y0) y1 = y0 + 1;
		if (y1>r->h) y1 = r->h;
		for (int x=0; x<dw; x++) {
			int x0 = (int)floorf(x * (float)r->w / dw);
			int x1 = (int)ceilf((x+1) * (float)r->w / dw);
			if (x1<=x0) x1 = x0 + 1;
			if (x1>r->w) x1 = r->w;
			long sum = 0;
			int n = 0;
			for (int yy=y0; yy<y1; yy++) for (int xx=x0; xx<x1; xx++) { sum += r->a[yy*r->w + xx]; n++; }
			out[y*dw + x] = n ? sum / n : 0;
		}
	}
}

typedef struct { char ch[TA_ROWS][TA_COLS]; uint8_t lv[TA_ROWS][TA_COLS]; } Grid; // lv: 0..100
static void gridPut(Grid* g, int x, int y, char c, int lv) {
	if (x>=0 && x<TA_COLS && y>=0 && y<TA_ROWS) { g->ch[y][x] = c; g->lv[y][x] = lv; }
}
static void gridText(Grid* g, int x, int y, const char* s, int lv) {
	for (int i=0; s[i]; i++) gridPut(g, x+i, y, s[i], lv);
}
static void gridCenter(Grid* g, int y, const char* s, int lv) {
	int len = strlen(s);
	if (len>TA_COLS) len = TA_COLS;
	gridText(g, (TA_COLS-len)/2, y, s, lv);
}

static uint8_t glyphs[128][CELL_W*CELL_H];
static uint8_t glyph_ready[128];
static const uint8_t* glyphFor(char c) { // a character's coverage, centered in a cell
	unsigned char u = (unsigned char)c;
	if (u>=128) u = '?';
	if (glyph_ready[u]) return glyphs[u];
	glyph_ready[u] = 1;
	memset(glyphs[u], 0, sizeof(glyphs[u]));
	if (u==' ') return glyphs[u];
	// the three characters the big letters are made of are drawn here, so they are crisp and always
	// fit their cell whatever the font: H (both halves), ' (the top half), . (the bottom half)
	if (u=='H' || u=='\'' || u=='.') {
		uint8_t* g = glyphs[u];
		for (int y=0; y<CELL_H; y++) for (int x=0; x<CELL_W; x++) {
			int on = 0;
			if (u=='H') on = ((x==1 || x==4) && y>=1 && y<=10) || ((x>=1 && x<=4) && (y==5 || y==6));
			else if (u=='\'') on = (x==2 || x==3) && y>=1 && y<=4;
			else on = (x==2 || x==3) && y>=9 && y<=10;
			g[y*CELL_W + x] = on ? 255 : 0;
		}
		return g;
	}
	SDL_Color white = {255,255,255,0};
	SDL_Surface* s = TTF_RenderGlyph_Blended(cell_font, u, white);
	if (!s) return glyphs[u];
	Uint32 amask = s->format->Amask, ashift = s->format->Ashift;
	int ox = (CELL_W - s->w) / 2;
	int oy = 0;
	for (int y=0; y<s->h; y++) {
		Uint32* row = (Uint32*)((Uint8*)s->pixels + y*s->pitch);
		for (int x=0; x<s->w; x++) {
			int cx = ox + x, cy = oy + y;
			if (cx<0 || cx>=CELL_W || cy<0 || cy>=CELL_H) continue;
			glyphs[u][cy*CELL_W + cx] = (row[x] & amask) >> ashift;
		}
	}
	SDL_FreeSurface(s);
	return glyphs[u];
}

// the layout of one tile: the title in big letters, its subtitle in small ones, a rule and the system
static void composeTile(const char* file, const char* badge, Grid* g) {
	memset(g, 0, sizeof(*g));
	for (int y=0; y<TA_ROWS; y++) memset(g->ch[y], ' ', TA_COLS);
	char title[256], main[256], sub[256];
	TA_title(file, title, sizeof(title));
	title[80] = '\0'; // far longer than any tile can show
	TA_split(title, main, sizeof(main), sub, sizeof(sub));
	char up[256];
	snprintf(up, sizeof(up), "%s", sub);
	for (char* p = up; *p; p++) *p = toupper((unsigned char)*p);
	char sub_lines[2][TA_LINE_MAX];
	int sub_count = up[0] ? TA_wrapPlain(up, TA_COLS-6, 2, sub_lines) : 0;
	TA_Layout lay;
	int max_rows = TA_ROWS - 8 - (sub_count ? sub_count + 1 : 0);
	if (!TA_layout(main, TA_COLS-4, max_rows, lineAspect, NULL, &lay) || lay.h<1) return;
	int total = lay.count * lay.h + (lay.count - 1);
	int block = total + (sub_count ? sub_count + 1 : 0);
	int top = (TA_ROWS - 3 - block) / 2;
	for (int li=0; li<lay.count; li++) {
		Raster* r = rasterLine(lay.lines[li]);
		if (!r) continue;
		int wcols = (int)lroundf(lay.h * 2.0f * r->w / r->h);
		if (wcols<1) wcols = 1;
		if (wcols>TA_COLS-2) wcols = TA_COLS - 2;
		uint8_t* px = malloc((size_t)wcols * lay.h * 2);
		char* chars = malloc((size_t)wcols * lay.h);
		if (px && chars) {
			shrink(r, wcols, lay.h*2, px);
			TA_pack(px, wcols, lay.h, chars);
			int ox = (TA_COLS - wcols) / 2, oy = top + li*(lay.h+1);
			for (int y=0; y<lay.h; y++) for (int x=0; x<wcols; x++) {
				char c = chars[y*wcols + x];
				if (c!=' ') gridPut(g, ox+x, oy+y, c, 100);
			}
		}
		free(px);
		free(chars);
	}
	for (int i=0; i<sub_count; i++) gridCenter(g, top + total + 1 + i, sub_lines[i], 70);
	for (int x=2; x<TA_COLS-2; x++) gridPut(g, x, TA_ROWS-4, '-', 40);
	char tag[96];
	snprintf(tag, sizeof(tag), "[ %s ]", badge);
	gridCenter(g, TA_ROWS-2, tag, 60);
}

static uint32_t tile_pixels[TILE*TILE];
static int writeTile(const Grid* g, const char* path) { // 0 if the PNG is on disk
	memset(tile_pixels, 0, sizeof(tile_pixels));
	SDL_Color bright = theme.white, dim = theme.dark_text;
	for (int cy=0; cy<TA_ROWS; cy++) for (int cx=0; cx<TA_COLS; cx++) {
		char c = g->ch[cy][cx];
		if (c==' ') continue;
		int lv = g->lv[cy][cx];
		uint32_t r = dim.r + (bright.r - dim.r) * lv / 100;
		uint32_t gg = dim.g + (bright.g - dim.g) * lv / 100;
		uint32_t b = dim.b + (bright.b - dim.b) * lv / 100;
		const uint8_t* cov = glyphFor(c);
		for (int y=0; y<CELL_H; y++) for (int x=0; x<CELL_W; x++) {
			uint32_t a = cov[y*CELL_W + x];
			if (!a) continue;
			uint32_t* p = &tile_pixels[(cy*CELL_H + y)*TILE + cx*CELL_W + x];
			if (a > (*p >> 24)) *p = (a << 24) | (r << 16) | (gg << 8) | b;
		}
	}
	SDL_Surface* s = SDL_CreateRGBSurfaceFrom(tile_pixels, TILE, TILE, 32, TILE*4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
	if (!s) return -1;
	char part[MAX_PATH];
	snprintf(part, sizeof(part), "%s.part", path);
	int ok = IMG_SavePNG(s, part)==0 && rename(part, path)==0;
	SDL_FreeSurface(s);
	if (!ok) unlink(part);
	return ok ? 0 : -1;
}


int TAGen_open(void) {
	if (big_font && cell_font) return 0;
	if (!TTF_WasInit()) TTF_Init();
	if (!big_font) big_font = TTF_OpenFont(FONT_PATH, BIG_PX);
	if (!cell_font) cell_font = TTF_OpenFont(FONT_PATH, CELL_PX);
	return (big_font && cell_font) ? 0 : -1;
}
void TAGen_close(void) {
	for (int i=0; i<12; i++) { free(rasters[i].a); rasters[i].a = NULL; rasters[i].text[0] = '\0'; }
	if (big_font) { TTF_CloseFont(big_font); big_font = NULL; }
	if (cell_font) { TTF_CloseFont(cell_font); cell_font = NULL; }
	memset(glyph_ready, 0, sizeof(glyph_ready));
}
int TAGen_write(const char* file, const char* badge, const char* art_path) {
	if (TAGen_open()) return -1;
	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%s", art_path);
	char* slash = strrchr(dir, '/');
	if (slash) *slash = '\0';
	if (mkdirs(dir)) return -1;
	Grid* g = malloc(sizeof(Grid));
	if (!g) return -1;
	composeTile(file, badge, g);
	int r = writeTile(g, art_path);
	free(g);
	return r;
}
int TAGen_artPath(const char* rom_path, char* out, size_t size) {
	const char* slash = strrchr(rom_path, '/');
	if (!slash) return -1;
	int dir_len = slash - rom_path;
	snprintf(out, size, "%.*s/.res/%s.png", dir_len, rom_path, slash + 1);
	return 0;
}
void TAGen_record(const char* art_path) { // appended: the tool's list reads the last line for a path
	struct stat st;
	if (stat(art_path, &st)) return;
	FILE* f = fopen(TAGEN_MADE_PATH, "a");
	if (!f) return;
	fprintf(f, "%ld\t%s\n", (long)st.st_mtime, art_path);
	fclose(f);
}
int TAGen_showArt(void) {
	char flag[8] = "";
	getFile(SHOW_ART_PATH, flag, sizeof(flag));
	return flag[0]!='0';
}
int TAGen_forRom(const char* rom_path) {
	char art[MAX_PATH];
	if (TAGen_artPath(rom_path, art, sizeof(art))) return -1;
	if (exists(art)) return 0;
	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%s", rom_path);
	char* slash = strrchr(dir, '/');
	if (!slash) return -1;
	*slash = '\0';
	const char* folder = strrchr(dir, '/');
	char badge[96];
	TA_badge(folder ? folder + 1 : dir, badge, sizeof(badge));
	if (TAGen_write(slash + 1, badge, art)) return -1;
	TAGen_record(art);
	return 1;
}
