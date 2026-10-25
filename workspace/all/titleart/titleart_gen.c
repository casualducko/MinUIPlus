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
#define FILL_BOTH 1           // grid codes for the marks of the big letters (TA_pack's H ' . become these)
#define FILL_TOP 2
#define FILL_BOTTOM 3
#define CONDENSE 0.8f         // the big letters are drawn this much narrower, so a long word still gets tall letters

// the tile

typedef struct { char text[TA_LINE_MAX]; int w, h; uint8_t* a; } Raster;
static TTF_Font* big_font = NULL;
static TTF_Font* cell_font = NULL;
static TTF_Font* big_fallback = NULL;  // for Japanese and the like: the same fonts' fallback (see GFX_renderText)
static TTF_Font* cell_fallback = NULL;
static Raster rasters[12];
static int raster_next = 0;

static Raster* rasterLine(const char* text) { // the line in big letters: its coverage, cropped
	for (int i=0; i<12; i++) if (rasters[i].a && !strcmp(rasters[i].text, text)) return &rasters[i];
	SDL_Color white = {255,255,255,0};
	SDL_Surface* s = GFX_renderTextWith(big_font, big_fallback, text, white);
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
	return r ? CONDENSE * r->w / r->h : 0;
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

#define CONT 0xFFFFFFFFu // the second column of a wide character
typedef struct { uint32_t ch[TA_ROWS][TA_COLS]; uint8_t lv[TA_ROWS][TA_COLS]; uint8_t big[TA_ROWS][TA_COLS]; } Grid; // lv: 0..100; big: a character drawn double size (it fills this cell and the ones right of and below it)
static void gridPut(Grid* g, int x, int y, uint32_t c, int lv) {
	if (x>=0 && x<TA_COLS && y>=0 && y<TA_ROWS) { g->ch[y][x] = c; g->lv[y][x] = lv; }
}
static void gridText(Grid* g, int x, int y, const char* s, int lv) { // UTF-8; a wide character takes two columns
	for (const char* p = s; *p; ) {
		uint32_t cp;
		int n = UTF8_next(p, &cp);
		if (n<=0) break;
		int w = TA_cellWidth(cp);
		if (w==2 && x+1>=TA_COLS) break; // no room for the second half
		gridPut(g, x, y, cp, lv);
		if (w==2) gridPut(g, x+1, y, CONT, lv);
		x += w;
		p += n;
	}
}
static void gridTextBig(Grid* g, int x, int y, const char* s, int lv) { // double size: each character takes 2x2 cells (a wide one 4x2)
	for (const char* p = s; *p; ) {
		uint32_t cp;
		int n = UTF8_next(p, &cp);
		if (n<=0) break;
		int w = TA_cellWidth(cp) * 2;
		if (x+w>TA_COLS || y+1>=TA_ROWS) break;
		for (int dy=0; dy<2; dy++) for (int dx=0; dx<w; dx++) gridPut(g, x+dx, y+dy, CONT, lv);
		gridPut(g, x, y, cp, lv);
		g->big[y][x] = 1;
		x += w;
		p += n;
	}
}
static void gridCenter(Grid* g, int y, const char* s, int lv);
static void gridCenterBig(Grid* g, int y, const char* s, int lv) { // too wide for double size: normal size on the lower row
	int cells = TA_cells(s) * 2;
	if (cells>TA_COLS-2) { gridCenter(g, y+1, s, lv); return; }
	gridTextBig(g, (TA_COLS-cells)/2, y, s, lv);
}
static void gridCenter(Grid* g, int y, const char* s, int lv) {
	int cells = TA_cells(s);
	if (cells>TA_COLS) cells = TA_COLS;
	gridText(g, (TA_COLS-cells)/2, y, s, lv);
}

// a character's coverage, centered in its one or two cells (CELL_W px each)
typedef struct { uint32_t cp; int cells; uint8_t cov[CELL_W*2*CELL_H]; } Glyph;
static Glyph ascii_glyphs[128];
static uint8_t ascii_ready[128];
static Glyph other_glyphs[256];
static int other_count = 0;
static const Glyph* glyphFor(uint32_t cp) {
	Glyph* gl;
	if (cp<128) {
		gl = &ascii_glyphs[cp];
		if (ascii_ready[cp]) return gl;
		ascii_ready[cp] = 1;
	}
	else {
		for (int i=0; i<other_count; i++) if (other_glyphs[i].cp==cp) return &other_glyphs[i];
		gl = &other_glyphs[other_count<256 ? other_count++ : 0]; // a full cache starts over at the front
	}
	memset(gl, 0, sizeof(*gl));
	gl->cp = cp;
	gl->cells = cp<0xFFFFFFFFu ? TA_cellWidth(cp) : 1;
	if (cp==' ' || cp==CONT) return gl;
	// the three marks the big letters are made of (FILL_BOTH, FILL_TOP, FILL_BOTTOM) are drawn here, so
	// they are crisp and always fit their cell whatever the font; real H ' . in text come from the font
	if (cp==FILL_BOTH || cp==FILL_TOP || cp==FILL_BOTTOM) {
		for (int y=0; y<CELL_H; y++) for (int x=0; x<CELL_W; x++) {
			int on = 0;
			if (cp==FILL_BOTH) on = ((x==1 || x==4) && y>=1 && y<=10) || ((x>=1 && x<=4) && (y==5 || y==6));
			else if (cp==FILL_TOP) on = (x==2 || x==3) && y>=1 && y<=4;
			else on = (x==2 || x==3) && y>=9 && y<=10;
			gl->cov[y*CELL_W*2 + x] = on ? 255 : 0;
		}
		return gl;
	}
	TTF_Font* use = NULL;
	if (cp<0x80 || (cp<=0xFFFF && TTF_GlyphIsProvided(cell_font, (Uint16)cp))) use = cell_font;
	else if (cell_fallback && cp<=0xFFFF && TTF_GlyphIsProvided(cell_fallback, (Uint16)cp)) use = cell_fallback;
	if (!use) return gl; // nobody has it: a blank cell
	SDL_Color white = {255,255,255,0};
	SDL_Surface* s = TTF_RenderGlyph_Blended(use, (Uint16)cp, white);
	if (!s) return gl;
	Uint32 amask = s->format->Amask, ashift = s->format->Ashift;
	int width = gl->cells * CELL_W;
	int ox = (width - s->w) / 2;
	int oy = (use==cell_font) ? 0 : TTF_FontAscent(cell_font) - TTF_FontAscent(use); // the same baseline
	for (int y=0; y<s->h; y++) {
		Uint32* row = (Uint32*)((Uint8*)s->pixels + y*s->pitch);
		for (int x=0; x<s->w; x++) {
			int cx = ox + x, cy = oy + y;
			if (cx<0 || cx>=width || cy<0 || cy>=CELL_H) continue;
			gl->cov[cy*CELL_W*2 + cx] = (row[x] & amask) >> ashift;
		}
	}
	SDL_FreeSurface(s);
	return gl;
}

// the layout of one tile: the title in big letters, its subtitle in small ones, a rule and the system
static void composeTile(const char* file, const char* badge, Grid* g) {
	memset(g, 0, sizeof(*g));
	for (int y=0; y<TA_ROWS; y++) for (int x=0; x<TA_COLS; x++) g->ch[y][x] = ' ';
	char title[256], main[256], sub[256];
	TA_title(file, title, sizeof(title));
	TA_truncate(title, 80); // far longer than any tile can show (and never cut inside a character)
	TA_split(title, main, sizeof(main), sub, sizeof(sub));
	char up[256];
	snprintf(up, sizeof(up), "%s", sub);
	for (char* p = up; *p; p++) *p = toupper((unsigned char)*p);
	char sub_lines[2][TA_LINE_MAX];
	int sub_count = up[0] ? TA_wrapPlain(up, (TA_COLS-6)/2, 2, sub_lines) : 0; // double size: half as many characters fit
	int sub_rows = sub_count*2;
	TA_Layout lay;
	int max_rows = TA_ROWS - 8 - (sub_count ? sub_rows + 1 : 0);
	int wide = 0; // kana and kanji have strokes too fine for letters five rows high: those titles are plain text
	for (const char* p = main; *p; ) {
		uint32_t cp;
		int n = UTF8_next(p, &cp);
		if (n<=0) break;
		if (TA_cellWidth(cp)==2) wide = 1;
		p += n;
	}
	if (wide || !TA_layout(main, TA_COLS-4, max_rows, lineAspect, NULL, &lay)) {
		// too long for letters this big: the whole title in plain characters instead, never a cut one
		char upper[256];
		snprintf(upper, sizeof(upper), "%s", main);
		for (char* p = upper; *p; p++) *p = toupper((unsigned char)*p);
		char plain[TA_MAX_LINES][TA_LINE_MAX];
		// double size when the whole title fits that way (never a cut one), else the small plain letters
		int sub_block = sub_count ? sub_rows + 1 : 0;
		int max_big = MIN(TA_MAX_LINES, (TA_ROWS - 8 - sub_block) / 2);
		int n = max_big>0 ? TA_wrapPlain(upper, (TA_COLS-8)/2, max_big, plain) : 0;
		int big = n>0 && plain[n-1][strlen(plain[n-1])-1]!='~';
		if (!big) n = TA_wrapPlain(upper, TA_COLS-8, TA_MAX_LINES, plain);
		int block = (big ? n*2 : n) + sub_block;
		int top = (TA_ROWS - 3 - block) / 2;
		for (int i=0; i<n; i++) {
			if (big) gridCenterBig(g, top + i*2, plain[i], 100);
			else gridCenter(g, top + i, plain[i], 100);
		}
		if (big) n *= 2; // rows used, for the subtitle below
		for (int i=0; i<sub_count; i++) gridCenterBig(g, top + n + 1 + i*2, sub_lines[i], 70);
		for (int x=2; x<TA_COLS-2; x++) gridPut(g, x, TA_ROWS-4, '-', 40);
		char tag[96];
		snprintf(tag, sizeof(tag), "[ %s ]", badge);
		gridCenterBig(g, TA_ROWS-3, tag, 60);
		return;
	}
	int total = lay.count * lay.h + (lay.count - 1);
	int block = total + (sub_count ? sub_rows + 1 : 0);
	int top = (TA_ROWS - 3 - block) / 2;
	for (int li=0; li<lay.count; li++) {
		Raster* r = rasterLine(lay.lines[li]);
		if (!r) continue;
		int wcols = (int)lroundf(lay.h * 2.0f * CONDENSE * r->w / r->h);
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
				if (c!=' ') gridPut(g, ox+x, oy+y, c=='H' ? FILL_BOTH : c=='\'' ? FILL_TOP : FILL_BOTTOM, 100);
			}
		}
		free(px);
		free(chars);
	}
	for (int i=0; i<sub_count; i++) gridCenterBig(g, top + total + 1 + i*2, sub_lines[i], 70);
	for (int x=2; x<TA_COLS-2; x++) gridPut(g, x, TA_ROWS-4, '-', 40);
	char tag[96];
	snprintf(tag, sizeof(tag), "[ %s ]", badge);
	gridCenterBig(g, TA_ROWS-3, tag, 60);
}

static uint32_t tile_pixels[TILE*TILE];
static int writeTile(const Grid* g, const char* path) { // 0 if the PNG is on disk
	memset(tile_pixels, 0, sizeof(tile_pixels));
	SDL_Color bright = theme.white, dim = theme.dark_text;
	for (int cy=0; cy<TA_ROWS; cy++) for (int cx=0; cx<TA_COLS; cx++) {
		uint32_t cp = g->ch[cy][cx];
		if (cp==' ' || cp==CONT) continue;
		int lv = g->lv[cy][cx];
		uint32_t r = dim.r + (bright.r - dim.r) * lv / 100;
		uint32_t gg = dim.g + (bright.g - dim.g) * lv / 100;
		uint32_t b = dim.b + (bright.b - dim.b) * lv / 100;
		const Glyph* gl = glyphFor(cp);
		int k = g->big[cy][cx] ? 2 : 1; // double size: every pixel of the glyph is drawn 2 x 2
		int width = gl->cells * CELL_W * k;
		for (int y=0; y<CELL_H*k; y++) for (int x=0; x<width; x++) {
			uint32_t a = gl->cov[(y/k)*CELL_W*2 + x/k];
			if (!a) continue;
			int px = cx*CELL_W + x;
			if (px>=TILE || cy*CELL_H + y>=TILE) continue;
			uint32_t* p = &tile_pixels[(cy*CELL_H + y)*TILE + px];
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
	if (exists(FALLBACK_FONT_PATH)) {
		if (!big_fallback) big_fallback = TTF_OpenFont(FALLBACK_FONT_PATH, BIG_PX);
		if (!cell_fallback) cell_fallback = TTF_OpenFont(FALLBACK_FONT_PATH, CELL_PX);
	}
	return (big_font && cell_font) ? 0 : -1;
}
void TAGen_close(void) {
	for (int i=0; i<12; i++) { free(rasters[i].a); rasters[i].a = NULL; rasters[i].text[0] = '\0'; }
	if (big_font) { TTF_CloseFont(big_font); big_font = NULL; }
	if (cell_font) { TTF_CloseFont(cell_font); cell_font = NULL; }
	if (big_fallback) { TTF_CloseFont(big_fallback); big_fallback = NULL; }
	if (cell_fallback) { TTF_CloseFont(cell_fallback); cell_fallback = NULL; }
	memset(ascii_ready, 0, sizeof(ascii_ready));
	other_count = 0;
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
