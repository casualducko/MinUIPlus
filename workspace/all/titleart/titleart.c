// Title art: makes a picture of each game's title, in big ASCII letters, for the launcher to show
// next to the game (it draws <Roms folder>/.res/<file name>.png). A game that already has art is
// never touched; the tiles made here are remembered in titleart-made.txt so they can be made again
// (after a theme change) without touching art that was put there by hand.
// A makes the missing ones, X makes the tool's own again, B quits (or cancels).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "titleart_core.h"

#define TILE 360
#define CELL_W 6
#define CELL_H 12
#define BIG_PX 96             // the title is drawn this big, then shrunk to the letter grid
#define CELL_PX 11            // the font size of the characters in the tile
#define MADE_PATH USERDATA_PATH "/titleart-made.txt"
#define MAX_GAMES 8192
#define MAX_MADE 8192

///////////////////////////////
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

///////////////////////////////
// the games and what is made

typedef struct { uint8_t folder; char file[200]; uint8_t has_art; } Game;
static char folders[128][128];
static int folder_count = 0;
static Game* games = NULL;
static int game_count = 0;

typedef struct { long mtime; char path[MAX_PATH]; } Made;
static Made* made = NULL;
static int made_count = 0;

static int isGameFile(const char* name) {
	if (name[0]=='.') return 0;
	const char* dot = strrchr(name, '.');
	if (!dot || !dot[1]) return 0; // no extension, not a game
	static const char* skip[] = {"png","jpg","jpeg","bmp","gif","txt","cfg","sav","srm","bak","xml","db","dat","log","pdf","cht","ini","sh","st","state","rtc",NULL};
	for (int i=0; skip[i]; i++) if (!strcasecmp(dot+1, skip[i])) return 0;
	if (!strncasecmp(dot+1, "st", 2) && isdigit((unsigned char)dot[3])) return 0; // save states: .st0
	return 1;
}
static void artPath(const Game* g, char* out, size_t size) {
	snprintf(out, size, ROMS_PATH "/%s/.res/%s.png", folders[g->folder], g->file);
}
static long fileMtime(const char* path) {
	struct stat st;
	return stat(path, &st)==0 ? (long)st.st_mtime : -1;
}
static void scanGames(void) {
	free(games);
	games = calloc(MAX_GAMES, sizeof(Game));
	game_count = 0;
	folder_count = 0;
	if (!games) return;
	DIR* roms = opendir(ROMS_PATH);
	if (!roms) return;
	struct dirent* fe;
	while ((fe = readdir(roms)) && folder_count<128) {
		if (fe->d_name[0]=='.') continue;
		char dir[MAX_PATH];
		snprintf(dir, sizeof(dir), ROMS_PATH "/%s", fe->d_name);
		struct stat st;
		if (stat(dir, &st) || !S_ISDIR(st.st_mode)) continue;
		DIR* d = opendir(dir);
		if (!d) continue;
		int fi = folder_count++;
		snprintf(folders[fi], sizeof(folders[fi]), "%s", fe->d_name);
		struct dirent* e;
		while ((e = readdir(d)) && game_count<MAX_GAMES) {
			if (!isGameFile(e->d_name) || strlen(e->d_name)>=sizeof(games[0].file)) continue;
			char full[MAX_PATH];
			snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
			if (stat(full, &st) || !S_ISREG(st.st_mode)) continue;
			Game* g = &games[game_count++];
			g->folder = fi;
			snprintf(g->file, sizeof(g->file), "%s", e->d_name);
			char art[MAX_PATH];
			artPath(g, art, sizeof(art));
			g->has_art = exists(art);
		}
		closedir(d);
	}
	closedir(roms);
}
static void loadMade(void) {
	free(made);
	made = calloc(MAX_MADE, sizeof(Made));
	made_count = 0;
	if (!made) return;
	char* text = allocFile(MADE_PATH);
	if (!text) return;
	char* save = NULL;
	for (char* line = strtok_r(text, "\n", &save); line && made_count<MAX_MADE; line = strtok_r(NULL, "\n", &save)) {
		char* tab = strchr(line, '\t');
		if (!tab || !tab[1]) continue;
		*tab = '\0';
		made[made_count].mtime = atol(line);
		snprintf(made[made_count].path, sizeof(made[made_count].path), "%s", tab+1);
		made_count++;
	}
	free(text);
}
static void saveMade(void) {
	size_t cap = (size_t)made_count * (MAX_PATH + 24) + 16;
	char* text = malloc(cap);
	if (!text) return;
	size_t len = 0;
	text[0] = '\0';
	for (int i=0; i<made_count; i++) {
		if (!exists(made[i].path)) continue; // the file was deleted
		len += snprintf(text+len, cap-len, "%ld\t%s\n", made[i].mtime, made[i].path);
	}
	putFile(MADE_PATH, text);
	free(text);
}
static int madeIndex(const char* path) {
	for (int i=0; i<made_count; i++) if (!strcmp(made[i].path, path)) return i;
	return -1;
}
static int isMine(int i) { // made here and not replaced since (a FAT clock is 2 seconds wide)
	long now = fileMtime(made[i].path);
	return now>=0 && labs(now - made[i].mtime) <= 2;
}

typedef struct { char art[MAX_PATH]; char file[200]; char folder[128]; } Job;

///////////////////////////////
// drawing

static void text(SDL_Surface* screen, TTF_Font* f, const char* s, SDL_Color color, int x, int y, int max_w) {
	if (!s || !*s) return;
	SDL_Surface* t = TTF_RenderUTF8_Blended(f, s, color);
	if (!t) return;
	SDL_BlitSurface(t, &(SDL_Rect){0,0,MIN(t->w, max_w),t->h}, screen, &(SDL_Rect){x,y});
	SDL_FreeSurface(t);
}

enum { ST_HOME, ST_WORK, ST_DONE };

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();
	big_font = TTF_OpenFont(FONT_PATH, BIG_PX);
	cell_font = TTF_OpenFont(FONT_PATH, CELL_PX);
	if (!big_font || !cell_font) LOG_error("titleart: can't open the font\n");
	loadMade();
	scanGames();

	int row_h = SCALE1(PILL_SIZE);
	int w = screen->w - SCALE1(PADDING*2);
	int quit = 0, dirty = 1, show_setting = 0;
	int state = ST_HOME;
	Job* jobs = NULL;
	int job_count = 0, job_done = 0, job_failed = 0;
	char note[160] = "";

	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		char art_flag[8] = "";
		getFile(SHOW_ART_PATH, art_flag, sizeof(art_flag));
		int art_shown = art_flag[0]!='0';
		int missing = 0, mine = 0;
		for (int i=0; i<game_count; i++) if (!games[i].has_art) missing++;
		for (int i=0; i<made_count; i++) if (isMine(i)) mine++;

		if (state==ST_HOME) {
			if (PAD_justPressed(BTN_B)) quit = 1;
			else if (PAD_justPressed(BTN_Y)) { // show or hide the game art in the launcher
				art_shown = !art_shown;
				putFile(SHOW_ART_PATH, art_shown ? "1\n" : "0\n");
				dirty = 1;
			}
			else if ((PAD_justPressed(BTN_A) && missing>0) || (PAD_justPressed(BTN_X) && mine>0)) {
				int remake = !(PAD_justPressed(BTN_A));
				free(jobs);
				jobs = calloc(remake ? MAX_MADE : MAX_GAMES, sizeof(Job));
				job_count = job_done = job_failed = 0;
				if (jobs && remake) {
					for (int i=0; i<made_count && job_count<MAX_MADE; i++) {
						if (!isMine(i)) continue;
						Job* j = &jobs[job_count++];
						snprintf(j->art, sizeof(j->art), "%s", made[i].path);
						char* res = strstr(j->art, "/.res/");
						if (!res) { job_count--; continue; }
						snprintf(j->file, sizeof(j->file), "%s", res + 6);
						char* ext = strrchr(j->file, '.'); // the .png
						if (ext) *ext = '\0';
						char dir[MAX_PATH];
						snprintf(dir, sizeof(dir), "%.*s", (int)(res - j->art), j->art);
						char* slash = strrchr(dir, '/');
						snprintf(j->folder, sizeof(j->folder), "%s", slash ? slash+1 : dir);
					}
				}
				else if (jobs) {
					for (int i=0; i<game_count; i++) {
						if (games[i].has_art) continue;
						Job* j = &jobs[job_count++];
						artPath(&games[i], j->art, sizeof(j->art));
						snprintf(j->file, sizeof(j->file), "%s", games[i].file);
						snprintf(j->folder, sizeof(j->folder), "%s", folders[games[i].folder]);
					}
				}
				if (job_count>0) { state = ST_WORK; PWR_disableAutosleep(); }
				dirty = 1;
			}
		}
		else if (state==ST_WORK) {
			if (PAD_justPressed(BTN_B) || job_done>=job_count) {
				int cancelled = job_done<job_count;
				saveMade();
				PWR_enableAutosleep();
				int made_ok = job_done - job_failed;
				snprintf(note, sizeof(note), "%s %i tile%s.", cancelled ? "Stopped after" : "Made", made_ok, made_ok==1 ? "" : "s");
				if (job_failed) snprintf(note + strlen(note), sizeof(note) - strlen(note), "\n%i failed.", job_failed);
				state = ST_DONE;
				scanGames();
				dirty = 1;
			}
			else {
				Job* j = &jobs[job_done];
				char dir[MAX_PATH];
				snprintf(dir, sizeof(dir), "%s", j->art);
				char* slash = strrchr(dir, '/');
				if (slash) *slash = '\0';
				char badge[96];
				TA_badge(j->folder, badge, sizeof(badge));
				Grid* g = malloc(sizeof(Grid));
				int ok = 0;
				if (g && !mkdirs(dir)) {
					composeTile(j->file, badge, g);
					ok = writeTile(g, j->art)==0;
				}
				free(g);
				if (ok) {
					long mt = fileMtime(j->art);
					int mi = madeIndex(j->art);
					if (mi<0 && made_count<MAX_MADE) { mi = made_count++; snprintf(made[mi].path, sizeof(made[mi].path), "%s", j->art); }
					if (mi>=0) made[mi].mtime = mt;
				}
				else job_failed++;
				job_done++;
				dirty = 1;
			}
		}
		else if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) { state = ST_HOME; dirty = 1; }

		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			int tx = SCALE1(PADDING+BUTTON_PADDING);
			char out[160];
			GFX_truncateText(font.large, "Title Art", out, screen->w - SCALE1(PADDING*2) - ow, SCALE1(BUTTON_PADDING*2));
			text(screen, font.large, out, COLOR_LIGHT_TEXT, tx, SCALE1(PADDING)+4, screen->w);
			int y = SCALE1(PADDING) + row_h + SCALE1(8);
			if (state==ST_HOME) {
				char line[160];
				snprintf(line, sizeof(line), "%i game%s without art", missing, missing==1 ? "" : "s");
				text(screen, font.large, line, COLOR_WHITE, tx, y, w);
				snprintf(line, sizeof(line), "%i tile%s made by this tool", mine, mine==1 ? "" : "s");
				text(screen, font.large, line, COLOR_WHITE, tx, y + row_h, w);
				text(screen, font.small, "A picture of each game's title for the launcher.", COLOR_GRAY, tx, y + row_h*2 + SCALE1(8), w);
				text(screen, font.small, "Art that is already there is never replaced.", COLOR_GRAY, tx, y + row_h*2 + SCALE1(26), w);
				if (mine>0) text(screen, font.small, "X makes this tool's tiles again (after a theme change).", COLOR_GRAY, tx, y + row_h*2 + SCALE1(44), w);
				snprintf(line, sizeof(line), "Art in the launcher: %s   (Y: %s)", art_shown ? "On" : "Off", art_shown ? "text only" : "show it");
				text(screen, font.large, line, art_shown ? COLOR_WHITE : COLOR_LIGHT_TEXT, tx, y + row_h*4 + SCALE1(8), w);
				if (!missing && !mine) text(screen, font.small, "Every game has art.", COLOR_LIGHT_TEXT, tx, y + row_h*2 + SCALE1(66), w);
				char* hints[5] = {NULL};
				hints[0] = "B"; hints[1] = "BACK";
				if (missing>0) { hints[2] = "A"; hints[3] = "MAKE"; }
				GFX_blitButtonGroup(hints, 1, screen, 1);
			}
			else if (state==ST_WORK) {
				int bw = w - SCALE1(BUTTON_PADDING*2);
				int by = screen->h/2 - row_h;
				Job* j = &jobs[MIN(job_done, job_count-1)];
				char title[256], shown[256];
				TA_title(j->file, title, sizeof(title));
				GFX_truncateText(font.large, title, shown, bw, 0);
				text(screen, font.large, shown, COLOR_WHITE, tx, by, bw);
				char line[96];
				snprintf(line, sizeof(line), "%i of %i", job_done, job_count);
				text(screen, font.small, line, COLOR_LIGHT_TEXT, tx, by + row_h, bw);
				int bar_y = by + row_h*2;
				GFX_blitPill(ASSET_BAR_BG_MENU, screen, &(SDL_Rect){tx, bar_y, bw, SCALE1(SETTINGS_SIZE)});
				if (job_done>0) GFX_blitPill(ASSET_BAR, screen, &(SDL_Rect){tx, bar_y, MAX(bw * job_done / job_count, SCALE1(SETTINGS_SIZE)), SCALE1(SETTINGS_SIZE)});
				GFX_blitButtonGroup((char*[]){"B","STOP",NULL}, 0, screen, 1);
			}
			else {
				GFX_blitMessage(font.large, note, screen, &(SDL_Rect){0, SCALE1(PADDING), screen->w, screen->h - SCALE1(PILL_SIZE+PADDING)});
				GFX_blitButtonGroup((char*[]){"A","OKAY",NULL}, 1, screen, 1);
			}
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

	for (int i=0; i<12; i++) free(rasters[i].a);
	free(jobs);
	free(games);
	free(made);
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
