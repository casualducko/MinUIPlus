// Themes: pick the colors of the launcher, the menus and the tools, see them live, and save them to
// .userdata/<platform>/theme.cfg (what GFX_init reads, see THEME_parse in api.c).
// The list: a preset, the nine colors, Save and apply, Reset to stock.
// A color: Up/Down pick red, green or blue; Left/Right change it, L1/R1 by more; A opens a palette,
// X undoes, Y goes back to the stock color, B returns.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <unistd.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"

#define THEME_FILE USERDATA_PATH "/theme.cfg"
#define SLOT_COUNT 9
#define PALETTE_COLS 8
#define PALETTE_ROWS 4

typedef struct {
	const char* name;
	const char* hint;
	size_t offset; // in ThemeColors
	SDL_Color stock;
} Slot;
static const Slot slots[SLOT_COUNT] = {
	{"Background",         "Behind everything",                  offsetof(ThemeColors, background),  {TRIAD_BLACK}},
	{"Text and selection", "Text, the selected row and the bars", offsetof(ThemeColors, white),       {TRIAD_WHITE}},
	{"Text on selection",  "Text on the selected row",           offsetof(ThemeColors, black),       {TRIAD_BLACK}},
	{"Rows",               "Dark pills and option rows",         offsetof(ThemeColors, dark_gray),   {TRIAD_DARK_GRAY}},
	{"Soft text",          "Titles and values",                  offsetof(ThemeColors, light_text),  {TRIAD_LIGHT_TEXT}},
	{"Dim text",           "Hints and unselected details",       offsetof(ThemeColors, dark_text),   {TRIAD_DARK_TEXT}},
	{"Accent",             "Underlines and small details",       offsetof(ThemeColors, gray),        {TRIAD_GRAY}},
	{"Button labels",      "The button hints at the bottom",     offsetof(ThemeColors, button_text), {TRIAD_GRAY}},
	{"Dots",               "Page dots and small marks",          offsetof(ThemeColors, light_gray),  {TRIAD_LIGHT_GRAY}},
};
// in the order of the slots above
static const struct { const char* name; unsigned rgb[SLOT_COUNT]; } presets[] = {
	{"Stock",  {0x000000,0xffffff,0x000000,0x262626,0xcccccc,0x666666,0x999999,0x999999,0x7f7f7f}},
	{"Warm",   {0x1a0f05,0xffd98c,0x1a0f05,0x3a2210,0xffeebb,0xa07840,0xc8a060,0xc8a060,0xa07840}},
	{"Amber",  {0x100800,0xffb000,0x100800,0x2a1a00,0xffcc55,0x7a5400,0xcc8c00,0xcc8c00,0x7a5400}},
	{"Ocean",  {0x06141f,0xd9f1ff,0x06141f,0x123247,0xa8d4ee,0x4f7a96,0x7fb2d1,0x7fb2d1,0x4f7a96}},
	{"Forest", {0x0d1a10,0xe3f5d0,0x0d1a10,0x1e3524,0xbfe0a8,0x5b7a52,0x8fb27e,0x8fb27e,0x5b7a52}},
	{"Violet", {0x0b0714,0xf1e6ff,0x0b0714,0x241a3a,0xcdb9ee,0x6e5a8c,0x9d87c4,0x9d87c4,0x6e5a8c}},
	{"Midnight",  {0x0a0f24,0xe6ecff,0x0a0f24,0x1a2347,0xb4c2f0,0x5b6a9e,0x8393cc,0x8393cc,0x6272a6}},
	{"Teal",      {0x041a1d,0xcff8fb,0x041a1d,0x0d3a40,0x8fe0e8,0x3a7f86,0x62b8c0,0x62b8c0,0x3a7f86}},
	{"Mint",      {0x071a17,0xd9fff3,0x071a17,0x12382f,0xa6e8d3,0x4f8f7e,0x7fc4ae,0x7fc4ae,0x4f8f7e}},
	{"Rose",      {0x1c0a12,0xffe0ea,0x1c0a12,0x3d1a28,0xf0b8cb,0x9a5a72,0xcc8aa4,0xcc8aa4,0x9a5a72}},
	{"Crimson",   {0x140404,0xffd6d6,0x140404,0x331010,0xf2a5a5,0x8f4a4a,0xc77878,0xc77878,0x8f4a4a}},
	{"Sunset",    {0x1f0d1c,0xffd0a8,0x1f0d1c,0x45203a,0xffb38a,0xa0587a,0xe08a78,0xe08a78,0xa0587a}},
	{"Slate",     {0x14161a,0xe8ecf2,0x14161a,0x2b2f36,0xb6bdc9,0x6b7280,0x929aa8,0x929aa8,0x6b7280}},
	{"Terminal",  {0x000a00,0x33ff66,0x000a00,0x0a2a10,0x22cc55,0x117733,0x1fae4a,0x1fae4a,0x117733}},
	{"Game Boy",  {0x0f380f,0x9bbc0f,0x0f380f,0x306230,0x8bac0f,0x4d7a1d,0x8bac0f,0x8bac0f,0x6a8c10}},
	{"Neon",      {0x0a0014,0x00f5ff,0x0a0014,0x25003d,0xff4de1,0x7a2d99,0xb050e0,0xb050e0,0x7a2d99}},
};
#define PRESET_COUNT ((int)(sizeof(presets)/sizeof(presets[0])))

enum { ROW_PRESET, ROW_FIRST_SLOT, ROW_SAVE = ROW_FIRST_SLOT + SLOT_COUNT, ROW_RESET, ROW_COUNT };
enum { SCR_LIST, SCR_COLOR, SCR_PALETTE };
enum { MODAL_NONE, MODAL_LEAVE, MODAL_SAVED, MODAL_FAILED };

static ThemeColors work;  // what is being edited (and what `theme` shows, live)
static ThemeColors saved; // what the file holds
static char font_name[MAX_PATH] = "";

static SDL_Color* slotColor(ThemeColors* t, int i) { return (SDL_Color*)((char*)t + slots[i].offset); }
static int sameColor(const SDL_Color* a, const SDL_Color* b) { return a->r==b->r && a->g==b->g && a->b==b->b; }
static int sameTheme(const ThemeColors* a, const ThemeColors* b) {
	for (int i=0; i<SLOT_COUNT; i++) {
		if (!sameColor(slotColor((ThemeColors*)a, i), slotColor((ThemeColors*)b, i))) return 0;
	}
	return 1;
}
static void setRGB(SDL_Color* c, unsigned rgb) { c->r = (rgb>>16)&0xff; c->g = (rgb>>8)&0xff; c->b = rgb&0xff; }
static int currentPreset(void) { // the preset that matches the colors, or -1
	for (int p=0; p<PRESET_COUNT; p++) {
		int match = 1;
		for (int i=0; i<SLOT_COUNT && match; i++) {
			SDL_Color c; setRGB(&c, presets[p].rgb[i]);
			if (!sameColor(&c, slotColor(&work, i))) match = 0;
		}
		if (match) return p;
	}
	return -1;
}
static void applyLive(void) { // the whole tool (and so every screen) is drawn with the colors being edited
	theme = work;
	GFX_applyTheme();
}
static void loadFontName(void) { // keep a font= line the file already has
	char text[2048] = "";
	FILE* f = fopen(THEME_FILE, "r");
	if (!f) return;
	size_t n = fread(text, 1, sizeof(text)-1, f);
	text[n] = '\0';
	fclose(f);
	ThemeColors scratch = theme;
	THEME_parse(text, &scratch, font_name, sizeof(font_name));
}
static int save(void) {
	char text[2048];
	THEME_format(&work, font_name, text, sizeof(text));
	putFile(THEME_FILE, text);
	char check[2048] = "";
	getFile(THEME_FILE, check, sizeof(check));
	ThemeColors back = theme;
	char font[MAX_PATH] = "";
	THEME_parse(check, &back, font, sizeof(font));
	if (!sameTheme(&back, &work)) return 0;
	saved = work;
	return 1;
}

// the palette: grays on the first row, then eight hues, bright to dark
static unsigned paletteColor(int col, int row) {
	if (row==0) { int v = col * 255 / (PALETTE_COLS-1); return (v<<16) | (v<<8) | v; }
	static const float hues[PALETTE_COLS] = {0, 28, 52, 120, 180, 214, 268, 320};
	static const float sat[3] = {1.0f, 0.55f, 0.85f};
	static const float val[3] = {1.0f, 0.85f, 0.38f};
	float h = hues[col] / 60.0f, s = sat[row-1], v = val[row-1];
	int i = (int)floorf(h) % 6;
	float f = h - floorf(h), p = v*(1-s), q = v*(1-s*f), t = v*(1-s*(1-f)), r, g, b;
	switch (i) {
		case 0: r=v; g=t; b=p; break;
		case 1: r=q; g=v; b=p; break;
		case 2: r=p; g=v; b=t; break;
		case 3: r=p; g=q; b=v; break;
		case 4: r=t; g=p; b=v; break;
		default: r=v; g=p; b=q; break;
	}
	return ((unsigned)(r*255+0.5f)<<16) | ((unsigned)(g*255+0.5f)<<8) | (unsigned)(b*255+0.5f);
}

static void text(SDL_Surface* screen, TTF_Font* f, const char* s, SDL_Color color, int x, int y, int max_w, int right_align) {
	if (!s || !*s) return;
	SDL_Surface* t = GFX_renderText(f, s, color);
	if (!t) return;
	int w = MIN(t->w, max_w);
	SDL_BlitSurface(t, &(SDL_Rect){0,0,w,t->h}, screen, &(SDL_Rect){right_align ? x-w : x, y});
	SDL_FreeSurface(t);
}
static void swatch(SDL_Surface* screen, SDL_Rect r, SDL_Color c, int border) { // a color with an outline that shows on any background
	SDL_FillRect(screen, &r, SDL_MapRGB(screen->format, 0x88, 0x88, 0x88));
	SDL_Rect inner = {r.x+border, r.y+border, r.w-border*2, r.h-border*2};
	SDL_FillRect(screen, &inner, SDL_MapRGB(screen->format, c.r, c.g, c.b));
}
static void title(SDL_Surface* screen, const char* s, int ow) {
	char out[96];
	GFX_truncateText(font.large, s, out, screen->w - SCALE1(PADDING*2) - ow, SCALE1(BUTTON_PADDING*2));
	text(screen, font.large, out, COLOR_LIGHT_TEXT, SCALE1(PADDING+BUTTON_PADDING), SCALE1(PADDING)+4, screen->w, 0);
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();
	work = theme; // the colors in use: the file's, or the stock ones
	saved = work;
	loadFontName();

	int row_h = SCALE1(PILL_SIZE);
	int top = SCALE1(PADDING) + row_h;
	int bottom = screen->h - SCALE1(PADDING) - row_h * 2; // above the hint line and the buttons
	int visible = (bottom - top) / row_h;
	if (visible<1) visible = 1;
	int w = screen->w - SCALE1(PADDING*2);

	int quit = 0, dirty = 1, show_setting = 0;
	int screen_kind = SCR_LIST, modal = MODAL_NONE;
	int sel = 0, first = 0;
	int slot = 0, channel = 0;
	SDL_Color undo = work.white;
	int pal_col = 0, pal_row = 0;
	int apply_needed = 0;

	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (modal==MODAL_LEAVE) {
			if (PAD_justPressed(BTN_A)) quit = 1;
			else if (PAD_justPressed(BTN_B)) { modal = MODAL_NONE; dirty = 1; }
		}
		else if (modal!=MODAL_NONE) {
			if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) {
				if (modal==MODAL_SAVED) quit = 1; // the launcher starts again with the new colors
				modal = MODAL_NONE;
				dirty = 1;
			}
		}
		else if (screen_kind==SCR_LIST) {
			if (PAD_justPressed(BTN_B)) {
				if (sameTheme(&work, &saved)) quit = 1;
				else { modal = MODAL_LEAVE; dirty = 1; }
			}
			else if (PAD_justRepeated(BTN_UP)) { sel = (sel+ROW_COUNT-1) % ROW_COUNT; dirty = 1; }
			else if (PAD_justRepeated(BTN_DOWN)) { sel = (sel+1) % ROW_COUNT; dirty = 1; }
			else if (sel==ROW_PRESET && (PAD_justRepeated(BTN_LEFT) || PAD_justRepeated(BTN_RIGHT))) {
				int dir = PAD_justRepeated(BTN_LEFT) ? -1 : 1;
				int cur = currentPreset();
				int next = cur<0 ? (dir>0 ? 0 : PRESET_COUNT-1) : (cur + dir + PRESET_COUNT) % PRESET_COUNT;
				for (int i=0; i<SLOT_COUNT; i++) setRGB(slotColor(&work, i), presets[next].rgb[i]);
				apply_needed = 1;
				dirty = 1;
			}
			else if (PAD_justPressed(BTN_A)) {
				if (sel>=ROW_FIRST_SLOT && sel<ROW_FIRST_SLOT+SLOT_COUNT) {
					slot = sel - ROW_FIRST_SLOT;
					channel = 0;
					undo = *slotColor(&work, slot);
					screen_kind = SCR_COLOR;
				}
				else if (sel==ROW_SAVE || sel==ROW_PRESET) modal = save() ? MODAL_SAVED : MODAL_FAILED;
				else if (sel==ROW_RESET) {
					for (int i=0; i<SLOT_COUNT; i++) *slotColor(&work, i) = slots[i].stock;
					apply_needed = 1;
				}
				dirty = 1;
			}
		}
		else if (screen_kind==SCR_COLOR) {
			SDL_Color* c = slotColor(&work, slot);
			uint8_t* ch[3] = {&c->r, &c->g, &c->b};
			int step = 0;
			if (PAD_justPressed(BTN_B)) { screen_kind = SCR_LIST; dirty = 1; }
			else if (PAD_justRepeated(BTN_UP)) { channel = (channel+2) % 3; dirty = 1; }
			else if (PAD_justRepeated(BTN_DOWN)) { channel = (channel+1) % 3; dirty = 1; }
			else if (PAD_justRepeated(BTN_LEFT)) step = -4;
			else if (PAD_justRepeated(BTN_RIGHT)) step = 4;
			else if (PAD_justRepeated(BTN_L1)) step = -32;
			else if (PAD_justRepeated(BTN_R1)) step = 32;
			else if (PAD_justPressed(BTN_X)) { *c = undo; apply_needed = 1; dirty = 1; }
			else if (PAD_justPressed(BTN_Y)) { *c = slots[slot].stock; apply_needed = 1; dirty = 1; }
			else if (PAD_justPressed(BTN_A)) { screen_kind = SCR_PALETTE; dirty = 1; }
			if (step) {
				int v = *ch[channel] + step;
				*ch[channel] = v<0 ? 0 : v>255 ? 255 : v;
				apply_needed = 1;
				dirty = 1;
			}
		}
		else { // SCR_PALETTE
			if (PAD_justPressed(BTN_B)) { screen_kind = SCR_COLOR; dirty = 1; }
			else if (PAD_justRepeated(BTN_LEFT)) { pal_col = (pal_col+PALETTE_COLS-1) % PALETTE_COLS; dirty = 1; }
			else if (PAD_justRepeated(BTN_RIGHT)) { pal_col = (pal_col+1) % PALETTE_COLS; dirty = 1; }
			else if (PAD_justRepeated(BTN_UP)) { pal_row = (pal_row+PALETTE_ROWS-1) % PALETTE_ROWS; dirty = 1; }
			else if (PAD_justRepeated(BTN_DOWN)) { pal_row = (pal_row+1) % PALETTE_ROWS; dirty = 1; }
			else if (PAD_justPressed(BTN_A)) {
				setRGB(slotColor(&work, slot), paletteColor(pal_col, pal_row));
				apply_needed = 1;
				screen_kind = SCR_COLOR;
				dirty = 1;
			}
		}

		if (apply_needed) { applyLive(); apply_needed = 0; dirty = 1; }
		if (sel<first) first = sel;
		if (sel>=first+visible) first = sel-visible+1;

		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			char* hints[5] = {NULL};

			if (modal!=MODAL_NONE) {
				const char* msg = modal==MODAL_LEAVE ? "Leave without saving?\n\nYour changes will be lost."
					: modal==MODAL_SAVED ? "Saved.\n\nThe launcher starts again\nwith your colors."
					: "Couldn't save the theme.";
				GFX_blitMessage(font.large, (char*)msg, screen, &(SDL_Rect){0, SCALE1(PADDING), screen->w, screen->h - SCALE1(PILL_SIZE+PADDING)});
				if (modal==MODAL_LEAVE) { hints[0] = "B"; hints[1] = "STAY"; hints[2] = "A"; hints[3] = "LEAVE"; }
				else { hints[0] = "A"; hints[1] = "OKAY"; }
			}
			else if (screen_kind==SCR_LIST) {
				title(screen, "Themes", ow);
				for (int i=0; i<visible && first+i<ROW_COUNT; i++) {
					int r = first + i;
					int y = top + i*row_h;
					int selected = (r==sel);
					SDL_Color color = COLOR_WHITE;
					if (selected) {
						GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), y, w, row_h});
						color = COLOR_BLACK;
					}
					int ty = y + (row_h - TTF_FontHeight(font.large))/2;
					int tx = SCALE1(PADDING+BUTTON_PADDING);
					int right = SCALE1(PADDING) + w - SCALE1(BUTTON_PADDING);
					SDL_Color soft = selected ? COLOR_BLACK : COLOR_LIGHT_TEXT;
					if (r==ROW_PRESET) {
						int cur = currentPreset();
						text(screen, font.large, "Preset", color, tx, ty, w, 0);
						text(screen, font.large, cur<0 ? "Custom" : presets[cur].name, soft, right, ty, w, 1);
					}
					else if (r>=ROW_FIRST_SLOT && r<ROW_FIRST_SLOT+SLOT_COUNT) {
						int s = r - ROW_FIRST_SLOT;
						SDL_Color* c = slotColor(&work, s);
						int sw = SCALE1(34), sh = row_h * 6 / 10;
						swatch(screen, (SDL_Rect){right - sw, y + (row_h-sh)/2, sw, sh}, *c, SCALE1(2));
						char hex[16];
						snprintf(hex, sizeof(hex), "#%02x%02x%02x", c->r, c->g, c->b);
						int hex_w = 0;
						TTF_SizeUTF8(font.small, hex, &hex_w, NULL);
						text(screen, font.small, hex, soft, right - sw - SCALE1(BUTTON_PADDING), y + (row_h - TTF_FontHeight(font.small))/2, w, 1);
						char name[96];
						GFX_truncateText(font.large, slots[s].name, name, w - sw - hex_w - SCALE1(BUTTON_PADDING*4), SCALE1(BUTTON_PADDING*2));
						text(screen, font.large, name, color, tx, ty, w, 0);
					}
					else text(screen, font.large, r==ROW_SAVE ? "Save and apply" : "Reset to stock", color, tx, ty, w, 0);
				}
				if (ROW_COUNT>visible) {
					int sx = (screen->w - SCALE1(24))/2;
					int soy = SCALE1((PILL_SIZE - 4) / 2);
					if (first>0) GFX_blitAsset(ASSET_SCROLL_UP, NULL, screen, &(SDL_Rect){sx, SCALE1(PADDING) + soy});
					if (first+visible<ROW_COUNT) GFX_blitAsset(ASSET_SCROLL_DOWN, NULL, screen, &(SDL_Rect){sx, bottom + soy - SCALE1(2)});
				}
				const char* note = sel==ROW_PRESET ? "Left and right try a preset, A applies it"
					: (sel>=ROW_FIRST_SLOT && sel<ROW_FIRST_SLOT+SLOT_COUNT) ? slots[sel-ROW_FIRST_SLOT].hint
					: sel==ROW_SAVE ? "Writes theme.cfg; the launcher restarts with it"
					: "Back to the stock look (then Save to keep it)";
				text(screen, font.small, note, COLOR_GRAY, SCALE1(PADDING+BUTTON_PADDING), bottom + row_h/2 - SCALE1(6), w, 0); // under the scroll arrow
				hints[0] = "B"; hints[1] = "BACK";
				hints[2] = "A"; hints[3] = sel==ROW_SAVE ? "SAVE" : sel==ROW_RESET ? "RESET" : sel==ROW_PRESET ? "APPLY" : "EDIT";
			}
			else {
				SDL_Color* c = slotColor(&work, slot);
				title(screen, slots[slot].name, ow);
				swatch(screen, (SDL_Rect){SCALE1(PADDING), top, w, row_h*2}, *c, SCALE1(3));
				char hex[16];
				snprintf(hex, sizeof(hex), "#%02x%02x%02x", c->r, c->g, c->b);
				int hex_w = 0;
				TTF_SizeUTF8(font.large, hex, &hex_w, NULL);
				// the hex code on the swatch, in whichever of black and white reads better
				int lum = (c->r*30 + c->g*59 + c->b*11) / 100;
				SDL_Color on = lum>128 ? (SDL_Color){0,0,0,0} : (SDL_Color){255,255,255,0};
				text(screen, font.large, hex, on, SCALE1(PADDING) + (w-hex_w)/2, top + (row_h*2 - TTF_FontHeight(font.large))/2, w, 0);
				int y0 = top + row_h*2 + SCALE1(8);
				if (screen_kind==SCR_COLOR) {
					const char* names[3] = {"Red", "Green", "Blue"};
					uint8_t vals[3] = {c->r, c->g, c->b};
					SDL_Color tints[3] = {{230,60,60,0}, {60,200,80,0}, {70,120,240,0}};
					for (int i=0; i<3; i++) {
						int y = y0 + i*row_h;
						int selected = (i==channel);
						SDL_Color color = COLOR_WHITE;
						if (selected) {
							GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), y, w, row_h});
							color = COLOR_BLACK;
						}
						int ty = y + (row_h - TTF_FontHeight(font.large))/2;
						text(screen, font.large, names[i], color, SCALE1(PADDING+BUTTON_PADDING), ty, w, 0);
						char num[8];
						snprintf(num, sizeof(num), "%i", vals[i]);
						text(screen, font.large, num, color, SCALE1(PADDING) + w - SCALE1(BUTTON_PADDING), ty, w, 1);
						int bx = SCALE1(PADDING) + w * 28 / 100, bw = w * 46 / 100, bh = SCALE1(10), by = y + (row_h-bh)/2;
						SDL_FillRect(screen, &(SDL_Rect){bx, by, bw, bh}, SDL_MapRGB(screen->format, 0x40, 0x40, 0x40));
						SDL_FillRect(screen, &(SDL_Rect){bx, by, bw * vals[i] / 255, bh}, SDL_MapRGB(screen->format, tints[i].r, tints[i].g, tints[i].b));
					}
					text(screen, font.small, slots[slot].hint, COLOR_GRAY, SCALE1(PADDING+BUTTON_PADDING), screen->h - SCALE1(PADDING) - row_h - row_h + SCALE1(6), w, 0);
					text(screen, font.small, "X undo   Y stock color   L1 R1 big steps", COLOR_DARK_TEXT, SCALE1(PADDING+BUTTON_PADDING), y0 + 3*row_h + SCALE1(10), w, 0);
					hints[0] = "A"; hints[1] = "PALETTE"; hints[2] = "B"; hints[3] = "DONE";
				}
				else {
					int cell = w / PALETTE_COLS;
					int cell_h = MIN(cell, (bottom - y0) / PALETTE_ROWS);
					for (int r=0; r<PALETTE_ROWS; r++) for (int col=0; col<PALETTE_COLS; col++) {
						SDL_Color pc; setRGB(&pc, paletteColor(col, r));
						SDL_Rect cr = {SCALE1(PADDING) + col*cell + SCALE1(3), y0 + r*cell_h + SCALE1(3), cell - SCALE1(6), cell_h - SCALE1(6)};
						if (col==pal_col && r==pal_row) { // the cursor: a white frame around the cell
							SDL_FillRect(screen, &(SDL_Rect){cr.x-SCALE1(3), cr.y-SCALE1(3), cr.w+SCALE1(6), cr.h+SCALE1(6)}, SDL_MapRGB(screen->format, 255, 255, 255));
							SDL_FillRect(screen, &(SDL_Rect){cr.x-SCALE1(1), cr.y-SCALE1(1), cr.w+SCALE1(2), cr.h+SCALE1(2)}, SDL_MapRGB(screen->format, 0, 0, 0));
						}
						SDL_FillRect(screen, &cr, SDL_MapRGB(screen->format, pc.r, pc.g, pc.b));
					}
					hints[0] = "B"; hints[1] = "BACK"; hints[2] = "A"; hints[3] = "USE";
				}
			}
			GFX_blitButtonGroup(hints, 1, screen, 1);
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
