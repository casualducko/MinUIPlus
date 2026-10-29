// Screenshot gallery: the pictures the screenshot hotkey (MENU + SELECT) saved in Screenshots/<system>/,
// newest first. Left/Right: previous/next, Up/Down: ten at a time, A: just the picture (A or B back),
// X: delete (asks first), B: quit.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "gallery_core.h"

#define MAX_SHOTS 100000 // (the list grows as it is read; this only stops a runaway folder)
typedef struct { char path[MAX_PATH]; char system[64]; char file[256]; } Shot;
static Shot* shots = NULL;
static int count = 0;

static int byNewest(const void* a, const void* b) {
	const Shot* x = a; const Shot* y = b;
	int c = Shots_compare(x->file, y->file);
	return c ? c : strcmp(x->path, y->path);
}
static void load(void) {
	free(shots);
	shots = NULL;
	count = 0;
	int cap = 0;
	DIR* root = opendir(SDCARD_PATH "/Screenshots");
	if (!root) return;
	struct dirent* fe;
	while ((fe = readdir(root))) {
		if (fe->d_name[0]=='.') continue;
		char dir[MAX_PATH];
		snprintf(dir, sizeof(dir), SDCARD_PATH "/Screenshots/%s", fe->d_name);
		struct stat st;
		if (stat(dir, &st) || !S_ISDIR(st.st_mode)) continue;
		DIR* d = opendir(dir);
		if (!d) continue;
		struct dirent* e;
		while ((e = readdir(d)) && count<MAX_SHOTS) {
			if (!Shots_isImage(e->d_name) || strlen(e->d_name)>=sizeof(shots[0].file)) continue;
			if (count==cap) { // every picture is read before the sort, however many there are
				Shot* bigger = realloc(shots, sizeof(Shot) * (cap ? cap*2 : 256));
				if (!bigger) break;
				shots = bigger;
				cap = cap ? cap*2 : 256;
			}
			Shot* s = &shots[count++];
			snprintf(s->path, sizeof(s->path), "%s/%s", dir, e->d_name);
			snprintf(s->system, sizeof(s->system), "%s", fe->d_name);
			snprintf(s->file, sizeof(s->file), "%s", e->d_name);
		}
		closedir(d);
	}
	closedir(root);
	if (count>0) qsort(shots, count, sizeof(Shot), byNewest);
}

#define XRGB 0x00FF0000, 0x0000FF00, 0x000000FF, 0
// the picture, as large as fits in max_w x max_h: a small one by whole numbers (pixel art stays sharp),
// a large one averaged down. NULL if it can't be read.
static SDL_Surface* loadFit(const char* path, int max_w, int max_h) {
	SDL_Surface* img = IMG_Load(path);
	if (!img || img->w<=0 || img->h<=0) { if (img) SDL_FreeSurface(img); return NULL; }
	SDL_Surface* src = SDL_CreateRGBSurface(SDL_SWSURFACE, img->w, img->h, 32, XRGB);
	if (!src) { SDL_FreeSurface(img); return NULL; }
	SDL_BlitSurface(img, NULL, src, NULL);
	SDL_FreeSurface(img);
	double scale = fmin((double)max_w / src->w, (double)max_h / src->h);
	int dw, dh, factor = 0;
	if (scale>=2.0) { factor = (int)scale; dw = src->w * factor; dh = src->h * factor; }
	else { dw = (int)lround(src->w * scale); dh = (int)lround(src->h * scale); }
	if (dw<1) dw = 1;
	if (dh<1) dh = 1;
	SDL_Surface* out = SDL_CreateRGBSurface(SDL_SWSURFACE, dw, dh, 32, XRGB);
	if (!out) { SDL_FreeSurface(src); return NULL; }
	for (int y=0; y<dh; y++) {
		uint32_t* d = (uint32_t*)((uint8_t*)out->pixels + y*out->pitch);
		if (factor) {
			uint32_t* s = (uint32_t*)((uint8_t*)src->pixels + (y/factor)*src->pitch);
			for (int x=0; x<dw; x++) d[x] = s[x/factor];
			continue;
		}
		int y0 = (int)floor(y * (double)src->h / dh), y1 = (int)ceil((y+1) * (double)src->h / dh);
		if (y1<=y0) y1 = y0 + 1;
		if (y1>src->h) y1 = src->h;
		for (int x=0; x<dw; x++) {
			int x0 = (int)floor(x * (double)src->w / dw), x1 = (int)ceil((x+1) * (double)src->w / dw);
			if (x1<=x0) x1 = x0 + 1;
			if (x1>src->w) x1 = src->w;
			uint32_t r = 0, g = 0, b = 0, n = 0;
			for (int yy=y0; yy<y1; yy++) {
				uint32_t* s = (uint32_t*)((uint8_t*)src->pixels + yy*src->pitch);
				for (int xx=x0; xx<x1; xx++) { r += (s[xx]>>16)&0xFF; g += (s[xx]>>8)&0xFF; b += s[xx]&0xFF; n++; }
			}
			d[x] = n ? ((r/n)<<16 | (g/n)<<8 | (b/n)) : 0;
		}
	}
	SDL_FreeSurface(src);
	return out;
}

static void text(SDL_Surface* screen, TTF_Font* f, const char* s, SDL_Color color, int x, int y, int max_w, int right_align) {
	if (!s || !*s) return;
	SDL_Surface* t = GFX_renderText(f, s, color);
	if (!t) return;
	int w = MIN(t->w, max_w);
	SDL_BlitSurface(t, &(SDL_Rect){0,0,w,t->h}, screen, &(SDL_Rect){right_align ? x-w : x, y});
	SDL_FreeSurface(t);
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();
	load();

	int row_h = SCALE1(PILL_SIZE);
	int pad = SCALE1(PADDING);
	int quit = 0, dirty = 1, show_setting = 0;
	int index = 0, chrome = 1, confirm = 0;
	int loaded_index = -1, loaded_chrome = -1;
	SDL_Surface* view = NULL;

	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (confirm) {
			if (PAD_justPressed(BTN_A)) {
				if (unlink(shots[index].path)==0 || access(shots[index].path, F_OK)!=0) { // gone (or already gone)
					memmove(&shots[index], &shots[index+1], sizeof(Shot) * (count - index - 1));
					count--;
					if (index>=count) index = count - 1;
					if (index<0) index = 0;
				} // else it stays in the list: the card is read-only or the like
				confirm = 0;
				loaded_index = -1;
				dirty = 1;
			}
			else if (PAD_justPressed(BTN_B)) { confirm = 0; dirty = 1; }
		}
		else if (PAD_justPressed(BTN_B)) {
			if (!chrome && count>0) { chrome = 1; dirty = 1; }
			else quit = 1;
		}
		else if (count>0) {
			if (PAD_justRepeated(BTN_LEFT) && index>0) { index--; dirty = 1; }
			else if (PAD_justRepeated(BTN_RIGHT) && index<count-1) { index++; dirty = 1; }
			else if (PAD_justRepeated(BTN_UP) && index>0) { index = MAX(0, index-10); dirty = 1; }
			else if (PAD_justRepeated(BTN_DOWN) && index<count-1) { index = MIN(count-1, index+10); dirty = 1; }
			else if (PAD_justPressed(BTN_A)) { chrome = !chrome; dirty = 1; }
			else if (PAD_justPressed(BTN_X) && chrome) { confirm = 1; dirty = 1; }
		}

		if (dirty) {
			GFX_clearUI(screen);
			if (count==0) {
				GFX_blitMessage(font.large, "No screenshots yet.\nIn a game, press MENU + SELECT.", screen, &(SDL_Rect){0, pad, screen->w, screen->h - SCALE1(PILL_SIZE+PADDING)});
				GFX_blitButtonGroup((char*[]){"B","BACK",NULL}, 1, screen, 1);
			}
			else {
				Shot* s = &shots[index];
				int top = chrome ? row_h + SCALE1(8) : 0;
				int bottom = chrome ? screen->h - pad - row_h - SCALE1(8) : screen->h;
				int max_h = bottom - top;
				if (index!=loaded_index || chrome!=loaded_chrome) {
					if (view) SDL_FreeSurface(view);
					view = loadFit(s->path, screen->w, max_h);
					loaded_index = index;
					loaded_chrome = chrome;
				}
				if (view) SDL_BlitSurface(view, NULL, screen, &(SDL_Rect){(screen->w - view->w)/2, top + (max_h - view->h)/2});
				else GFX_blitMessage(font.large, "Can't read this picture.", screen, &(SDL_Rect){0, top, screen->w, max_h});
				if (chrome) {
					char game[256], when[64], title[256], line[160];
					Shots_parse(s->file, game, sizeof(game), when, sizeof(when));
					snprintf(line, sizeof(line), "%i / %i", index+1, count);
					int num_w = 0;
					TTF_SizeUTF8(font.medium, line, &num_w, NULL);
					GFX_truncateText(font.large, game, title, screen->w - pad*2 - num_w - SCALE1(BUTTON_PADDING*4), SCALE1(BUTTON_PADDING*2));
					text(screen, font.large, title, COLOR_WHITE, pad + SCALE1(BUTTON_PADDING), pad - SCALE1(PADDING) + SCALE1(4) + SCALE1(PADDING)/2, screen->w, 0);
					text(screen, font.medium, line, COLOR_LIGHT_TEXT, screen->w - pad - SCALE1(BUTTON_PADDING), pad - SCALE1(PADDING) + SCALE1(8) + SCALE1(PADDING)/2, screen->w, 1);
					snprintf(line, sizeof(line), "%s%s%s", when, when[0] ? "   " : "", s->system);
					text(screen, font.small, line, COLOR_GRAY, pad + SCALE1(BUTTON_PADDING), screen->h - pad - row_h + (row_h - TTF_FontHeight(font.small))/2, screen->w/2, 0);
					if (confirm) {
						GFX_clearUI(screen);
						char msg[400];
						snprintf(msg, sizeof(msg), "Delete this screenshot?\n%s", title);
						GFX_blitMessage(font.large, msg, screen, &(SDL_Rect){0, pad, screen->w, screen->h - SCALE1(PILL_SIZE+PADDING)});
						GFX_blitButtonGroup((char*[]){"B","NO","A","DELETE",NULL}, 1, screen, 1);
					}
					else GFX_blitButtonGroup((char*[]){"X","DELETE","B","BACK",NULL}, 1, screen, 1);
				}
			}
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

	if (view) SDL_FreeSurface(view);
	free(shots);
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
