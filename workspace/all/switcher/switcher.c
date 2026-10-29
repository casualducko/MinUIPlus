// Switcher: the games you played last as cards, each with the picture of its last save state, so you can
// jump from one game to another (like Onion's GameSwitcher). Left/Right: previous/next, A: resume the
// game (it starts where its last save state left off), X: take it off the list (the save state stays),
// Y: the picture full screen, SELECT: play time on or off, B: quit.
//
// The list is the launcher's "Recently Played" (recent.txt). A resumes by leaving the game's path in
// SWITCH_PATH; the launcher, which starts again when this tool ends, picks it up and opens the game the
// same way a press of A (or X, Resume) on it would, so multi-disc games and the save slot are handled there.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "switcher_core.h"

#define MAX_CARDS 60

typedef struct {
	char rel[SW_PATH_MAX];		// below the card, as recent.txt has it
	char title[160];
	char tag[32];				// the emulator tag: GBA, SFC...
	int slot;					// the save slot it resumes from, -1 for none
	char bmp[MAX_PATH];			// that slot's picture
	long saved_at;				// when the picture was made
	long seconds;				// play time
	int launches;
	char art[MAX_PATH];			// title art (.res), used when there is no picture
} Card;
static Card* cards = NULL;
static int card_count = 0;

static void load(void) {
	free(cards);
	cards = calloc(MAX_CARDS, sizeof(Card));
	card_count = 0;
	if (!cards) return;
	char* text = allocFile(RECENT_PATH);
	if (!text) return;
	char (*paths)[SW_PATH_MAX] = calloc(MAX_CARDS * 2, SW_PATH_MAX);
	int n = paths ? SW_parseRecents(text, paths, MAX_CARDS * 2) : 0;
	free(text);

	PlayRecord* records = calloc(PLAYTIME_MAX, sizeof(PlayRecord));
	int record_count = 0;
	char* ptext = allocFile(PLAYTIME_PATH);
	if (ptext && records) record_count = Playtime_parse(ptext, records, PLAYTIME_MAX);

	for (int i=0; i<n && card_count<MAX_CARDS; i++) {
		char sd_path[MAX_PATH];
		snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, paths[i]);
		if (!exists(sd_path)) continue; // a game that is gone
		char emu[MAX_PATH], emu_path[MAX_PATH];
		getEmuName(sd_path, emu);
		getEmuPath(emu, emu_path);
		if (!strncasecmp(emu, "P8", 2) || Emu_isNative(emu_path)) continue; // PICO-8 and the other native programs have no save states to switch between
		Card* c = &cards[card_count];
		snprintf(c->rel, sizeof(c->rel), "%s", paths[i]);
		const char* file = SW_baseName(sd_path);
		Playtime_name(sd_path, c->title, sizeof(c->title));
		snprintf(c->tag, sizeof(c->tag), "%s", emu);

		// the slot to resume from: the one the launcher would use (the last you saved to), or the auto-save
		// a quit or sleep made if that is newer; and that slot's picture
		int manual = -1;
		char slot_file[MAX_PATH], manual_bmp[MAX_PATH], auto_bmp[MAX_PATH];
		snprintf(slot_file, sizeof(slot_file), "%s/.minui/%s/%s.txt", SHARED_USERDATA_PATH, emu, file);
		char slot_text[16] = "";
		if (exists(slot_file)) {
			getFile(slot_file, slot_text, sizeof(slot_text));
			manual = SW_parseSlot(slot_text);
		}
		long manual_at = 0, auto_at = 0;
		struct stat st;
		manual_bmp[0] = '\0';
		if (manual>=0) {
			snprintf(manual_bmp, sizeof(manual_bmp), "%s/.minui/%s/%s.%d.bmp", SHARED_USERDATA_PATH, emu, file, manual);
			if (stat(manual_bmp, &st)==0) manual_at = (long)st.st_mtime;
			else manual_bmp[0] = '\0';
		}
		snprintf(auto_bmp, sizeof(auto_bmp), "%s/.minui/%s/%s.%d.bmp", SHARED_USERDATA_PATH, emu, file, AUTO_RESUME_SLOT);
		if (stat(auto_bmp, &st)==0) auto_at = (long)st.st_mtime;
		c->slot = SW_pickSlot(manual, manual_at, auto_at);
		if (c->slot==AUTO_RESUME_SLOT) { snprintf(c->bmp, sizeof(c->bmp), "%s", auto_bmp); c->saved_at = auto_at; }
		else if (c->slot>=0) { snprintf(c->bmp, sizeof(c->bmp), "%s", manual_bmp); c->saved_at = manual_at; }

		// title art, for a game with no picture
		const char* slash = strrchr(sd_path, '/');
		if (slash) snprintf(c->art, sizeof(c->art), "%.*s/.res/%s.png", (int)(slash - sd_path), sd_path, file);

		for (int k=0; k<record_count; k++) {
			if (!strcmp(records[k].path, sd_path) || !strcmp(records[k].path, paths[i])) { c->seconds = records[k].seconds; c->launches = records[k].launches; break; }
		}
		card_count++;
	}
	free(paths);
	free(records);
	free(ptext);
}

static int removeFromList(const char* rel) { // out of recent.txt; the save states are not touched. 1 if it is done
	char* text = allocFile(RECENT_PATH);
	if (!text) return 0;
	size_t cap = strlen(text) + 2;
	char* out = malloc(cap);
	int ok = 0;
	if (out) {
		int n = SW_dropRecent(text, rel, out, cap);
		ok = writeFileAtomic(RECENT_PATH, out, n)==0;
		free(out);
	}
	free(text);
	return ok;
}

#define XRGB 0x00FF0000, 0x0000FF00, 0x000000FF, 0
// the picture, as large as fits in max_w x max_h: a small one by whole numbers (pixel art stays sharp),
// a large one averaged down. NULL if it can't be read. (As in the screenshot gallery.)
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

// the name of a system for its folder tag (the tag itself when it is not one we know)
static const char* SW_systemName(const char* tag) {
	static const struct { const char* tag; const char* name; } names[] = {
		{"FC","NES"}, {"NES","NES"}, {"FDS","Famicom Disk System"}, {"GB","Game Boy"}, {"GBC","Game Boy Color"}, {"GBA","Game Boy Advance"},
		{"MGBA","Game Boy Advance"}, {"SGB","Super Game Boy"}, {"SFC","SNES"}, {"SNES","SNES"}, {"SUPER","SNES"}, {"SUPA","SNES"}, {"SNX","SNES"},
		{"MD","Genesis"}, {"GEN","Genesis"}, {"SMS","Master System"}, {"GG","Game Gear"}, {"PS","PlayStation"}, {"PCE","TurboGrafx-16"},
		{"NGP","Neo Geo Pocket"}, {"NGPC","Neo Geo Pocket Color"}, {"VB","Virtual Boy"}, {"PKM","Pokemon mini"},
		{"P8","PICO-8"}, {NULL,NULL}
	};
	for (int i=0; names[i].tag; i++) if (!strcasecmp(names[i].tag, tag)) return names[i].name;
	return tag;
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
	int index = 0, full = 0, confirm = 0, show_time = 1;
	int loaded_index = -1, loaded_full = -1;
	SDL_Surface* view = NULL;
	int view_is_art = 0;

	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (confirm) {
			if (PAD_justPressed(BTN_A)) {
				if (removeFromList(cards[index].rel)) { // (a card that could not be taken off the list stays on screen too)
					memmove(&cards[index], &cards[index+1], sizeof(Card) * (card_count - index - 1));
					card_count--;
					if (index>=card_count) index = card_count - 1;
					if (index<0) index = 0;
				}
				confirm = 0;
				loaded_index = -1;
				dirty = 1;
			}
			else if (PAD_justPressed(BTN_B)) { confirm = 0; dirty = 1; }
		}
		else if (PAD_justPressed(BTN_B)) {
			if (full) { full = 0; dirty = 1; }
			else quit = 1;
		}
		else if (card_count>0) {
			if (PAD_justRepeated(BTN_LEFT) || PAD_justRepeated(BTN_L1)) { index = (index + card_count - 1) % card_count; dirty = 1; }
			else if (PAD_justRepeated(BTN_RIGHT) || PAD_justRepeated(BTN_R1)) { index = (index + 1) % card_count; dirty = 1; }
			else if (PAD_justPressed(BTN_Y)) { full = !full; dirty = 1; }
			else if (PAD_justPressed(BTN_SELECT)) { show_time = !show_time; dirty = 1; }
			else if (PAD_justPressed(BTN_X) && !full) { confirm = 1; dirty = 1; }
			else if (PAD_justPressed(BTN_A)) {
				char line[SW_PATH_MAX + 8];
				if (cards[index].slot>=0) snprintf(line, sizeof(line), "%s\t%d", cards[index].rel, cards[index].slot); // resume from that state
				else snprintf(line, sizeof(line), "%s", cards[index].rel);
				putFile(SWITCH_PATH, line);
				quit = 1;
			}
		}

		if (dirty && !quit) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			if (card_count==0) {
				GFX_blitMessage(font.large, "No recent games yet.\n\nGames you play show up here,\nwith their last save state.", screen, &(SDL_Rect){0, pad, screen->w, screen->h - SCALE1(PILL_SIZE+PADDING)});
				GFX_blitButtonGroup((char*[]){"B","BACK",NULL}, 1, screen, 1);
			}
			else if (confirm) {
				char msg[400];
				snprintf(msg, sizeof(msg), "Take this game off the list?\n\n%s\n\n(Its save state stays.)", cards[index].title);
				GFX_blitMessage(font.large, msg, screen, &(SDL_Rect){0, pad, screen->w, screen->h - SCALE1(PILL_SIZE+PADDING)});
				GFX_blitButtonGroup((char*[]){"B","NO","A","REMOVE",NULL}, 1, screen, 1);
			}
			else {
				Card* c = &cards[index];
				int has_state = c->slot>=0; // a save state (it may have no picture)
				int has_picture = has_state && c->bmp[0];
				int top = full ? 0 : pad + row_h + SCALE1(6);
				int area_w = full ? screen->w : screen->w - pad*2 - SCALE1(BUTTON_PADDING*2);
				int area_h = full ? screen->h : screen->h - top - pad - row_h*2 - SCALE1(4);
				if (index!=loaded_index || full!=loaded_full) {
					if (view) SDL_FreeSurface(view);
					view = NULL;
					view_is_art = 0;
					if (has_picture) view = loadFit(c->bmp, area_w, area_h);
					if (!view && !full && c->art[0] && exists(c->art)) { view = loadFit(c->art, area_w, area_h); view_is_art = view!=NULL; }
					loaded_index = index;
					loaded_full = full;
				}
				int cx = (screen->w - area_w) / 2;
				if (view) {
					int vx = cx + (area_w - view->w)/2, vy = top + (area_h - view->h)/2;
					if (!full) { // a frame around the picture
						SDL_Rect frame = {vx-3, vy-3, view->w+6, view->h+6};
						SDL_FillRect(screen, &frame, SDL_MapRGB(screen->format, COLOR_GRAY.r, COLOR_GRAY.g, COLOR_GRAY.b));
					}
					SDL_BlitSurface(view, NULL, screen, &(SDL_Rect){vx, vy});
				}
				else GFX_blitMessage(font.large, c->title, screen, &(SDL_Rect){cx, top, area_w, area_h});

				if (!full) {
					char title[256], line[200];
					GFX_truncateText(font.large, c->title, title, screen->w - pad*2 - ow - SCALE1(BUTTON_PADDING*2), SCALE1(BUTTON_PADDING*2));
					text(screen, font.large, title, COLOR_WHITE, pad + SCALE1(BUTTON_PADDING), pad + SCALE1(4), screen->w, 0);
					if (card_count>1) { // the way round
						text(screen, font.large, "<", COLOR_LIGHT_TEXT, SCALE1(4), top + area_h/2 - row_h/2, pad, 0);
						text(screen, font.large, ">", COLOR_LIGHT_TEXT, screen->w - SCALE1(14), top + area_h/2 - row_h/2, pad, 0);
					}
					int ly = top + area_h + SCALE1(2);
					char when[64] = "";
					if (has_picture && (view && !view_is_art)) {
						char ago[48];
						SW_ago((long)time(NULL) - c->saved_at, ago, sizeof(ago));
						if (c->slot==AUTO_RESUME_SLOT) snprintf(when, sizeof(when), "%s", ago);
						else snprintf(when, sizeof(when), "Slot %d, %s", c->slot + 1, ago);
					}
					else if (has_state) snprintf(when, sizeof(when), c->slot==AUTO_RESUME_SLOT ? "Saved" : "Slot %d", c->slot + 1);
					else snprintf(when, sizeof(when), "No save yet");
					snprintf(line, sizeof(line), "%s  -  %s", SW_systemName(c->tag), when);
					text(screen, font.small, line, COLOR_LIGHT_TEXT, pad + SCALE1(BUTTON_PADDING), ly, screen->w - pad*2 - SCALE1(80), 0);
					snprintf(line, sizeof(line), "%i / %i", index+1, card_count);
					text(screen, font.small, line, COLOR_LIGHT_TEXT, screen->w - pad - SCALE1(BUTTON_PADDING), ly, screen->w, 1);
					if (show_time) {
						if (c->launches>0 || c->seconds>0) {
							char dur[32];
							Playtime_duration(c->seconds, dur, sizeof(dur));
							snprintf(line, sizeof(line), "Played %s", dur);
						}
						else snprintf(line, sizeof(line), "Not played yet");
						text(screen, font.small, line, COLOR_GRAY, pad + SCALE1(BUTTON_PADDING), ly + SCALE1(15), screen->w - pad*2, 0);
					}
					GFX_blitButtonGroup((char*[]){"B","BACK","X","REMOVE",NULL}, 0, screen, 0);
					GFX_blitButtonGroup((char*[]){"A",has_state ? "RESUME" : "PLAY","Y","FULL",NULL}, 1, screen, 1);
				}
			}
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

	if (view) SDL_FreeSurface(view);
	free(cards);
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
