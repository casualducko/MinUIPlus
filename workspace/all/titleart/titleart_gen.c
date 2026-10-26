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
#define TEXT_COLS (TA_COLS-10)  // the title never gets wider than this, so it keeps a gap to the label outline (about 12 px each side)
static float condense(void); // how much narrower than the font's own shape the big letters are drawn, so a long word still gets tall letters

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
	return r ? condense() * r->w / r->h : 0;
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
typedef struct { uint32_t ch[TA_ROWS][TA_COLS]; uint8_t lv[TA_ROWS][TA_COLS]; uint8_t big[TA_ROWS][TA_COLS]; } Grid; // lv: 0..100; big: a character drawn this many times its size (2, 3 or 4: it fills this cell and the ones right of and below it)
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
static void gridTextBigK(Grid* g, int x, int y, const char* s, int lv, int k) { // k times the size: each character takes k x k cells (a wide one 2k x k)
	for (const char* p = s; *p; ) {
		uint32_t cp;
		int n = UTF8_next(p, &cp);
		if (n<=0) break;
		int w = TA_cellWidth(cp) * k;
		if (x+w>TA_COLS || y+k-1>=TA_ROWS) break;
		for (int dy=0; dy<k; dy++) for (int dx=0; dx<w; dx++) gridPut(g, x+dx, y+dy, CONT, lv);
		gridPut(g, x, y, cp, lv);
		g->big[y][x] = k;
		x += w;
		p += n;
	}
}
static void gridTextBig(Grid* g, int x, int y, const char* s, int lv) { gridTextBigK(g, x, y, s, lv, 2); }
static void gridCenter(Grid* g, int y, const char* s, int lv);
static void gridCenterBigK(Grid* g, int y, const char* s, int lv, int k) {
	int cells = TA_cells(s) * k;
	if (cells>TA_COLS) cells = TA_COLS;
	gridTextBigK(g, (TA_COLS-cells)/2, y, s, lv, k);
}
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
			int bar = x<=1 || x>=4; // an H: two bars (2 px) and a crossbar; the halves are its upper and lower parts
			if (cp==FILL_BOTH) on = bar || y==5 || y==6;
			else if (cp==FILL_TOP) on = (bar && y<=5) || y==4 || y==5;
			else on = (bar && y>=6) || y==6 || y==7;
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
	int oy = (use==cell_font) ? 0 : TTF_FontAscent(cell_font) - TTF_FontAscent(use); // the same baseline
	if (s->w > width) { // wider than its cell (M, W): squeezed to fit, not cut off
		for (int y=0; y<s->h; y++) {
			Uint32* row = (Uint32*)((Uint8*)s->pixels + y*s->pitch);
			int cy = oy + y;
			if (cy<0 || cy>=CELL_H) continue;
			for (int cx=0; cx<width; cx++) {
				int x0 = cx * s->w / width, x1 = (cx+1) * s->w / width;
				if (x1<=x0) x1 = x0 + 1;
				unsigned sum = 0;
				for (int x=x0; x<x1 && x<s->w; x++) sum += (row[x] & amask) >> ashift;
				gl->cov[cy*CELL_W*2 + cx] = sum / (x1-x0);
			}
		}
	}
	else {
		int ox = (width - s->w) / 2;
		for (int y=0; y<s->h; y++) {
			Uint32* row = (Uint32*)((Uint8*)s->pixels + y*s->pitch);
			for (int x=0; x<s->w; x++) {
				int cx = ox + x, cy = oy + y;
				if (cx<0 || cx>=width || cy<0 || cy>=CELL_H) continue;
				gl->cov[cy*CELL_W*2 + cx] = (row[x] & amask) >> ashift;
			}
		}
	}
	SDL_FreeSurface(s);
	return gl;
}

// The big letters are drawn as a fine grid of square dots, DOT px apart, not as characters: their shape comes
// out of the font cleanly at any size. This is their coverage (0..255), blended in by writeTile.
#define DOT 3
static uint8_t letter_px[TILE*TILE];
// .userdata/<platform>/titleart-style picks the look of the big letters:
//   classic (the default, and the original look): a mark per half cell, H for a full one, ' on top, . below
//   ascii: characters of different weight on a finer grid;  solid: clean solid letters;  dots: solid with a gap
enum { STYLE_CLASSIC, STYLE_ASCII, STYLE_SOLID, STYLE_DOTS };
static int letterStyle(void) {
	static int style = -1;
	if (style<0) {
		char* text = allocFile(USERDATA_PATH "/titleart-style");
		style = STYLE_CLASSIC;
		if (text && !strncmp(text, "ascii", 5)) style = STYLE_ASCII;
		else if (text && !strncmp(text, "solid", 5)) style = STYLE_SOLID;
		else if (text && !strncmp(text, "dots", 4)) style = STYLE_DOTS;
		free(text);
	}
	return style;
}
static float condense(void) { return letterStyle()==STYLE_CLASSIC ? 0.8f : 0.9f; }
static Grid* tile_grid = NULL; // the tile being composed (the classic look puts its marks in it)
static void putDot(int px, int py, int a) { // one dot with its top left corner at px, py
	int gap = letterStyle()==STYLE_DOTS ? 1 : 0;
	for (int y=py; y<py+DOT-gap; y++) for (int x=px; x<px+DOT-gap; x++) if (x>=0 && x<TILE && y>=0 && y<TILE && a>letter_px[y*TILE+x]) letter_px[y*TILE+x] = a;
}

// The default look, real ASCII art: the title is cut from the big font into a fine grid of character cells
// (ASCII_CW x ASCII_CH px) and each cell gets a character whose weight matches how much of the cell the letter
// covers: " .:+#@". The characters are drawn in one box (the ink of a #), each keeping its own shape in it, so
// the edges of a letter are light marks and its body is heavy ones.
#define ASCII_CW 4
#define ASCII_CH 8
#define ASCII_MIN_ROWS 4       // letters shorter than this are too fine for characters: that title is plain text
static const char ascii_ramp[] = " .:+#@";
static TTF_Font* ramp_font = NULL;
static uint8_t ramp_cov[sizeof(ascii_ramp)][ASCII_CW*ASCII_CH];
static uint8_t ramp_ready = 0;
static void rampBuild(void) { // every character of the ramp, as coverage over a cell
	if (ramp_ready) return;
	if (!ramp_font) ramp_font = TTF_OpenFont(FONT_PATH, ASCII_CH + 2);
	if (!ramp_font) return;
	SDL_Color white = {255,255,255,0};
	int rx0 = 0, ry0 = 0, iw = 0, ih = 0;
	SDL_Surface* ref = TTF_RenderGlyph_Blended(ramp_font, '#', white); // the box all characters share
	if (!ref) return;
	{
		Uint32 am = ref->format->Amask, as = ref->format->Ashift;
		int x0 = ref->w, y0 = ref->h, x1 = -1, y1 = -1;
		for (int y=0; y<ref->h; y++) {
			Uint32* row = (Uint32*)((Uint8*)ref->pixels + y*ref->pitch);
			for (int x=0; x<ref->w; x++) if ((row[x] & am) >> as) { if (x<x0) x0 = x; if (x>x1) x1 = x; if (y<y0) y0 = y; if (y>y1) y1 = y; }
		}
		if (x1>=0) { rx0 = x0; ry0 = y0; iw = x1-x0+1; ih = y1-y0+1; }
	}
	SDL_FreeSurface(ref);
	if (iw<1 || ih<1) return;
	for (int k=1; k<(int)sizeof(ascii_ramp)-1; k++) { // not the NUL, and a space is blank
		SDL_Surface* sf = TTF_RenderGlyph_Blended(ramp_font, (Uint16)ascii_ramp[k], white);
		if (!sf) continue;
		Uint32 amask = sf->format->Amask, ashift = sf->format->Ashift;
		for (int y=0; y<ASCII_CH; y++) for (int x=0; x<ASCII_CW; x++) {
			int sy0 = ry0 + y*ih/ASCII_CH, sy1 = ry0 + (y+1)*ih/ASCII_CH; if (sy1<=sy0) sy1 = sy0 + 1;
			int sx0 = rx0 + x*iw/ASCII_CW, sx1 = rx0 + (x+1)*iw/ASCII_CW; if (sx1<=sx0) sx1 = sx0 + 1;
			unsigned sum = 0, n = 0;
			for (int yy=sy0; yy<sy1; yy++) for (int xx=sx0; xx<sx1; xx++) {
				n++;
				if (yy>=0 && yy<sf->h && xx>=0 && xx<sf->w) sum += (((Uint32*)((Uint8*)sf->pixels + yy*sf->pitch))[xx] & amask) >> ashift;
			}
			ramp_cov[k][y*ASCII_CW + x] = n ? sum/n : 0;
		}
		SDL_FreeSurface(sf);
	}
	ramp_ready = 1;
}
static void drawChars(const char* text, int row, int h, int level) {
	Raster* r = rasterLine(text);
	if (!r) return;
	rampBuild();
	if (!ramp_ready) return;
	int hpx = h * CELL_H;
	int ncol = (int)lroundf(hpx * condense() * r->w / r->h / ASCII_CW);
	if (ncol<1) ncol = 1;
	if (ncol*ASCII_CW > TEXT_COLS*CELL_W) ncol = TEXT_COLS*CELL_W/ASCII_CW;
	int nrow = hpx / ASCII_CH;
	uint8_t* px = malloc((size_t)ncol*nrow);
	if (!px) return;
	shrink(r, ncol, nrow, px);
	int ox = (TILE - ncol*ASCII_CW)/2, oy = row*CELL_H;
	const int nr = (int)sizeof(ascii_ramp)-1;
	for (int y=0; y<nrow; y++) for (int x=0; x<ncol; x++) {
		float c = px[y*ncol + x] / 255.0f;
		if (c < 0.20f) continue; // no mark
		int k = (int)(powf(c, 0.7f) * (nr-1) + 0.5f);
		if (k<1) k = 1;
		if (k>nr-1) k = nr-1;
		for (int yy=0; yy<ASCII_CH; yy++) for (int xx=0; xx<ASCII_CW; xx++) {
			int a = ramp_cov[k][yy*ASCII_CW + xx] * level / 100;
			int tx = ox + x*ASCII_CW + xx, ty = oy + y*ASCII_CH + yy;
			if (a && tx>=0 && tx<TILE && ty>=0 && ty<TILE && a>letter_px[ty*TILE+tx]) letter_px[ty*TILE+tx] = a;
		}
	}
	free(px);
}

// one line of letters h rows high, centered, its top at row `row`; level 0..100 is its brightness
static void drawLetters(const char* text, int row, int h, int level) {
	if (letterStyle()==STYLE_ASCII) { drawChars(text, row, h, level); return; }
	Raster* r = rasterLine(text);
	if (!r) return;
	if (letterStyle()==STYLE_CLASSIC) { // the original: the line on the character grid, one mark per half cell
		int wcols = (int)lroundf(h * 2.0f * condense() * r->w / r->h);
		if (wcols<1) wcols = 1;
		if (wcols>TEXT_COLS) wcols = TEXT_COLS;
		uint8_t* px = malloc((size_t)wcols * h * 2);
		char* chars = malloc((size_t)wcols * h);
		if (px && chars && tile_grid) {
			shrink(r, wcols, h*2, px);
			TA_pack(px, wcols, h, chars);
			int ox = (TA_COLS - wcols) / 2;
			for (int y=0; y<h; y++) for (int x=0; x<wcols; x++) {
				char c = chars[y*wcols + x];
				if (c!=' ') gridPut(tile_grid, ox+x, row+y, c=='H' ? FILL_BOTH : c=='\'' ? FILL_TOP : FILL_BOTTOM, level);
			}
		}
		free(px);
		free(chars);
		return;
	}
	int wcols = (int)lroundf(h * 2.0f * condense() * r->w / r->h);
	if (wcols<1) wcols = 1;
	if (wcols>TEXT_COLS) wcols = TEXT_COLS;
	int dw = wcols * (CELL_W/DOT), dh = h * (CELL_H/DOT); // the letters' dots
	uint8_t* px = malloc((size_t)dw * dh);
	if (!px) return;
	shrink(r, dw, dh, px);
	int ox = (TA_COLS - wcols) / 2 * CELL_W, oy = row * CELL_H;
	for (int y=0; y<dh; y++) for (int x=0; x<dw; x++) {
		int a = (px[y*dw + x] - 64) * 255 / 128; // soft threshold: below a quarter nothing, above three quarters solid
		if (a>255) a = 255;
		a = a * level / 100;
		if (a>0) putDot(ox + x*DOT, oy + y*DOT, a);
	}
	free(px);
}

static void subLine(Grid* g, int row, const char* text) { // the subtitle: double-size characters; in the solid and dots looks, letters two rows high (unless it has wide characters)
	if (letterStyle()==STYLE_ASCII || letterStyle()==STYLE_CLASSIC) { gridCenterBig(g, row, text, 70); return; }
	for (const char* p = text; *p; ) {
		uint32_t cp;
		int n = UTF8_next(p, &cp);
		if (n<=0) break;
		if (TA_cellWidth(cp)==2) { gridCenterBig(g, row, text, 70); return; }
		p += n;
	}
	drawLetters(text, row, 2, 80);
}

// the layout of one tile: the title in big letters, its subtitle in small ones, a rule and the system
static void composeTile(const char* file, const char* badge, Grid* g) {
	memset(g, 0, sizeof(*g));
	tile_grid = g;
	memset(letter_px, 0, sizeof(letter_px));
	for (int y=0; y<TA_ROWS; y++) for (int x=0; x<TA_COLS; x++) g->ch[y][x] = ' ';
	char title[256], main[256], sub[256];
	TA_title(file, title, sizeof(title));
	TA_truncate(title, 80); // far longer than any tile can show (and never cut inside a character)
	TA_split(title, main, sizeof(main), sub, sizeof(sub));
	char up[256];
	snprintf(up, sizeof(up), "%s", sub);
	for (char* p = up; *p; p++) *p = toupper((unsigned char)*p);
	char sub_lines[2][TA_LINE_MAX];
	int sub_count = up[0] ? TA_wrapPlain(up, TEXT_COLS/2, 2, sub_lines) : 0; // double size: half as many characters fit
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
	int chars = letterStyle()==STYLE_ASCII || letterStyle()==STYLE_CLASSIC;
	int fits = !wide && TA_layout(main, TEXT_COLS, max_rows, lineAspect, NULL, &lay);
	if (fits && chars && lay.h<ASCII_MIN_ROWS) fits = 0; // characters need taller letters than solid dots do
	if (!fits) {
		// too long for letters this big: the whole title in plain characters instead, never a cut one
		char upper[256];
		snprintf(upper, sizeof(upper), "%s", main);
		for (char* p = upper; *p; p++) *p = toupper((unsigned char)*p);
		char plain[TA_MAX_LINES][TA_LINE_MAX];
		// the biggest characters (3 or 2 times) the whole title fits in (never a cut one), else the small plain letters
		int sub_block = sub_count ? sub_rows + 1 : 0;
		int k = 1, n = 0;
		for (int kk=3; kk>=2 && k==1; kk--) {
			int max_big = MIN(TA_MAX_LINES, (TA_ROWS - 8 - sub_block) / kk);
			int m = max_big>0 ? TA_wrapPlain(upper, TEXT_COLS/kk, max_big, plain) : 0;
			if (m>0 && plain[m-1][strlen(plain[m-1])-1]!='~') { k = kk; n = m; }
		}
		if (k==1) n = TA_wrapPlain(upper, TEXT_COLS, TA_MAX_LINES, plain);
		int block = n*k + sub_block;
		int top = (TA_ROWS - 3 - block) / 2;
		for (int i=0; i<n; i++) {
			if (k>1) gridCenterBigK(g, top + i*k, plain[i], 100, k);
			else gridCenter(g, top + i, plain[i], 100);
		}
		n *= k; // rows used, for the subtitle below
		for (int i=0; i<sub_count; i++) subLine(g, top + n + 1 + i*2, sub_lines[i]);
		char tag[96];
		snprintf(tag, sizeof(tag), "[ %s ]", badge);
		gridCenterBig(g, TA_ROWS-3, tag, 60);
		return;
	}
	int total = lay.count * lay.h + (lay.count - 1);
	int block = total + (sub_count ? sub_rows + 1 : 0);
	int top = (TA_ROWS - 3 - block) / 2;
	for (int li=0; li<lay.count; li++) {
		drawLetters(lay.lines[li], top + li*(lay.h+1), lay.h, 100);
	}
	for (int i=0; i<sub_count; i++) subLine(g, top + total + 1 + i*2, sub_lines[i]);
	char tag[96];
	snprintf(tag, sizeof(tag), "[ %s ]", badge);
	gridCenterBig(g, TA_ROWS-3, tag, 60);
}

static uint32_t tile_pixels[TILE*TILE];
// The tile is drawn as the front of a cartridge: a thick outline with the top corners cut, a label outline inside it
// and grip ridges in the base beside the system name.
static void cartRect(uint32_t color, int x, int y, int w, int h) {
	for (int yy=y; yy<y+h; yy++) for (int xx=x; xx<x+w; xx++) if (xx>=0 && xx<TILE && yy>=0 && yy<TILE) tile_pixels[yy*TILE + xx] = color;
}
static void drawCart(uint32_t outer, uint32_t inner) {
	const int t = 4, cut = 16;
	cartRect(outer, cut, 0, TILE-2*cut, t);                  // top edge
	for (int i=0; i<cut; i++) {                              // the two cut corners
		cartRect(outer, cut-1-i, i, t, 1);
		cartRect(outer, TILE-cut-t+1+i, i, t, 1);
	}
	cartRect(outer, 0, cut-1, t, TILE-cut+1);                // sides
	cartRect(outer, TILE-t, cut-1, t, TILE-cut+1);
	cartRect(outer, 0, TILE-t, TILE, t);                     // bottom
	const int lx = 16, ly = 16, lw = TILE-2*16, lh = 300;    // the label
	cartRect(inner, lx+3, ly, lw-6, 2);
	cartRect(inner, lx+3, ly+lh-2, lw-6, 2);
	cartRect(inner, lx, ly+3, 2, lh-6);
	cartRect(inner, lx+lw-2, ly+3, 2, lh-6);
	for (int i=0; i<3; i++) {                                // grip ridges
		cartRect(inner, 26 + i*9, 328, 3, 20);
		cartRect(inner, TILE-29 - i*9, 328, 3, 20);
	}
}
static int writeTile(const Grid* g, const char* path) { // 0 if the PNG is on disk
	memset(tile_pixels, 0, sizeof(tile_pixels));
	SDL_Color bright = theme.white, dim = theme.dark_text;
	{
		uint32_t o = 0xFF000000u | ((dim.r + (bright.r - dim.r) * 75 / 100) << 16) | ((dim.g + (bright.g - dim.g) * 75 / 100) << 8) | (dim.b + (bright.b - dim.b) * 75 / 100);
		uint32_t n = 0xFF000000u | ((dim.r + (bright.r - dim.r) * 45 / 100) << 16) | ((dim.g + (bright.g - dim.g) * 45 / 100) << 8) | (dim.b + (bright.b - dim.b) * 45 / 100);
		drawCart(o, n);
	}
	for (int cy=0; cy<TA_ROWS; cy++) for (int cx=0; cx<TA_COLS; cx++) {
		uint32_t cp = g->ch[cy][cx];
		if (cp==' ' || cp==CONT) continue;
		int lv = g->lv[cy][cx];
		uint32_t r = dim.r + (bright.r - dim.r) * lv / 100;
		uint32_t gg = dim.g + (bright.g - dim.g) * lv / 100;
		uint32_t b = dim.b + (bright.b - dim.b) * lv / 100;
		const Glyph* gl = glyphFor(cp);
		int k = g->big[cy][cx] ? g->big[cy][cx] : 1; // bigger: every pixel of the glyph is drawn k x k
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
	for (int i=0; i<TILE*TILE; i++) { // the big letters
		uint32_t a = letter_px[i];
		if (a > (tile_pixels[i] >> 24)) tile_pixels[i] = (a << 24) | ((uint32_t)bright.r << 16) | ((uint32_t)bright.g << 8) | bright.b;
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
	if (ramp_font) { TTF_CloseFont(ramp_font); ramp_font = NULL; }
	ramp_ready = 0;
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
