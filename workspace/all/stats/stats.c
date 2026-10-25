// Play stats: how long each game has been played, from the shared playtime.txt that minarch keeps.
// Up/Down scroll, L/R page, X changes the order (most played / recently played), B quits.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"

static PlayRecord* records = NULL;
static int count = 0;
static int by_recent = 0;

static int byTime(const void* a, const void* b) {
	const PlayRecord* x = a; const PlayRecord* y = b;
	if (y->seconds!=x->seconds) return y->seconds>x->seconds ? 1 : -1;
	return strcmp(x->path, y->path);
}
static int byRecent(const void* a, const void* b) {
	const PlayRecord* x = a; const PlayRecord* y = b;
	if (y->last!=x->last) return y->last>x->last ? 1 : -1;
	return strcmp(x->path, y->path);
}
static void load(void) {
	records = calloc(PLAYTIME_MAX, sizeof(PlayRecord));
	char* text = calloc(1, PLAYTIME_MAX * 600);
	if (!records || !text) { free(text); return; }
	FILE* f = fopen(PLAYTIME_PATH, "r");
	if (f) {
		size_t n = fread(text, 1, PLAYTIME_MAX*600 - 1, f);
		text[n] = '\0';
		fclose(f);
		count = Playtime_parse(text, records, PLAYTIME_MAX);
	}
	free(text);
}
static void sortRecords(void) {
	qsort(records, count, sizeof(PlayRecord), by_recent ? byRecent : byTime);
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
	sortRecords();

	long total = 0;
	for (int i=0; i<count; i++) total += records[i].seconds;

	int row_h = SCALE1(PILL_SIZE);
	int top = SCALE1(PADDING) + row_h; // under the title
	int bottom = screen->h - SCALE1(PADDING) - row_h - row_h; // above the details line and the button hints
	int visible = (bottom - top) / row_h;
	if (visible<1) visible = 1;

	int quit = 0, dirty = 1, show_setting = 0, sel = 0, first = 0;
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (PAD_justPressed(BTN_B)) quit = 1;
		else if (count>0 && PAD_justRepeated(BTN_UP)) { sel = (sel+count-1) % count; dirty = 1; }
		else if (count>0 && PAD_justRepeated(BTN_DOWN)) { sel = (sel+1) % count; dirty = 1; }
		else if (count>0 && PAD_justRepeated(BTN_LEFT)) { sel = MAX(0, sel-visible); dirty = 1; }
		else if (count>0 && PAD_justRepeated(BTN_RIGHT)) { sel = MIN(count-1, sel+visible); dirty = 1; }
		else if (PAD_justPressed(BTN_X)) { by_recent = !by_recent; sortRecords(); sel = first = 0; dirty = 1; }

		if (sel<first) first = sel;
		if (sel>=first+visible) first = sel-visible+1;

		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			char title[96];
			char dur[32];
			Playtime_duration(total, dur, sizeof(dur));
			if (count) snprintf(title, sizeof(title), "%s in %i game%s", dur, count, count==1 ? "" : "s");
			else snprintf(title, sizeof(title), "Play Stats");
			char out[96];
			GFX_truncateText(font.large, title, out, screen->w - SCALE1(PADDING*2) - ow, SCALE1(BUTTON_PADDING*2));
			text(screen, font.large, out, COLOR_LIGHT_TEXT, SCALE1(PADDING+BUTTON_PADDING), SCALE1(PADDING)+4, screen->w, 0);

			if (!count) {
				text(screen, font.large, "Nothing played yet.", COLOR_WHITE, SCALE1(PADDING+BUTTON_PADDING), top, screen->w, 0);
				text(screen, font.small, "Time is counted while a game runs,", COLOR_GRAY, SCALE1(PADDING+BUTTON_PADDING), top+row_h, screen->w, 0);
				text(screen, font.small, "starting from this version.", COLOR_GRAY, SCALE1(PADDING+BUTTON_PADDING), top+row_h+SCALE1(16), screen->w, 0);
			}
			int w = screen->w - SCALE1(PADDING*2);
			for (int i=0; i<visible && first+i<count; i++) {
				PlayRecord* r = &records[first+i];
				int y = top + i*row_h;
				int selected = (first+i==sel);
				SDL_Color color = COLOR_WHITE;
				if (selected) {
					GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), y, w, row_h});
					color = COLOR_BLACK;
				}
				char name[256];
				Playtime_name(r->path, name, sizeof(name));
				Playtime_duration(r->seconds, dur, sizeof(dur));
				int dur_w = 0;
				TTF_SizeUTF8(font.large, dur, &dur_w, NULL);
				char name_out[256];
				GFX_truncateText(font.large, name, name_out, w - dur_w - SCALE1(BUTTON_PADDING*3), SCALE1(BUTTON_PADDING*2));
				int ty = y + (row_h - TTF_FontHeight(font.large))/2;
				text(screen, font.large, name_out, color, SCALE1(PADDING+BUTTON_PADDING), ty, w, 0);
				text(screen, font.large, dur, selected ? COLOR_BLACK : COLOR_LIGHT_TEXT, SCALE1(PADDING)+w-SCALE1(BUTTON_PADDING), ty, w, 1);
			}
			if (count) { // the details of the selected game
				PlayRecord* r = &records[sel];
				char when[48] = "never";
				if (r->last>0) {
					time_t t = (time_t)r->last;
					struct tm tmv;
					if (localtime_r(&t, &tmv)) {
						char month[16];
						strftime(month, sizeof(month), "%b", &tmv);
						snprintf(when, sizeof(when), "%s %i, %i", month, tmv.tm_mday, tmv.tm_year+1900);
					}
				}
				char detail[160];
				snprintf(detail, sizeof(detail), "Started %i time%s, last played %s", r->launches, r->launches==1 ? "" : "s", when);
				text(screen, font.small, detail, COLOR_GRAY, SCALE1(PADDING+BUTTON_PADDING), screen->h - SCALE1(PADDING) - row_h - row_h + SCALE1(6), screen->w - SCALE1(PADDING*2), 0);
			}
			GFX_blitButtonGroup((char*[]){"X", by_recent ? "MOST PLAYED" : "RECENT", "B","BACK",NULL}, 1, screen, 1);
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

	free(records);
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
