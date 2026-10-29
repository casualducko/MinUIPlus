// Controls: every button that does something on the screens that don't say so, one page per screen.
// Left/Right turn the page (Up/Down too), B quits.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"

typedef struct { const char* key; const char* does; } Row;
typedef struct { const char* title; Row rows[12]; } Page;

// (Fn is the button the screens call MENU on other devices)
static const Page pages[] = {
	{ "Game list", {
		{ "A", "Open the game or folder" },
		{ "B", "Back" },
		{ "Y", "A random game" },
		{ "X", "Resume (when shown)" },
		{ "START", "Add the game to a collection" },
		{ "", "On a collection: delete it" },
		{ "SELECT", "Tools" },
		{ "L1 / R1", "Previous or next letter" },
		{ "Left / Right", "A page up or down" },
		{ "Fn", "The game switcher" },
		{ "Power", "Sleep" },
		{ NULL, NULL } } },
	{ "In a game", {
		{ "Fn", "The game menu" },
		{ "Fn + SELECT", "Take a screenshot" },
		{ "Fn, twice", "Save, then the switcher" },
		{ "Fn + volume", "Brightness" },
		{ "Volume", "Louder or quieter" },
		{ "Left / Right", "Menu: pick the save slot" },
		{ "Quit", "Saves to slots 9 and 8" },
		{ "Restart", "Start over (asks first)" },
		{ NULL, NULL } } },
	{ "Game switcher", {
		{ "A", "Resume the game" },
		{ "B", "Back" },
		{ "Left / Right", "The previous or next game" },
		{ "L1 / R1", "The same" },
		{ "Y", "Picture full screen" },
		{ "X", "Take the game off the list" },
		{ "SELECT", "Show or hide the play time" },
		{ NULL, NULL } } },
	{ "Romulus", {
		{ "A", "Open, or get the game" },
		{ "Fn or X", "Search by name" },
		{ "Y", "A random game" },
		{ "SELECT", "Newest first, or A to Z" },
		{ "L1 / R1", "A page up or down" },
		{ "B", "Back" },
		{ NULL, NULL } } },
	{ "PicoSearch", {
		{ "A", "Open, or get the cart" },
		{ "Fn or X", "Search" },
		{ "Y", "Shuffle, or the sort order" },
		{ "SELECT", "Next view (list, grid)" },
		{ "L1 / R1", "A page up or down" },
		{ "B", "Back" },
		{ NULL, NULL } } },
	{ "Typing", {
		{ "D-pad", "Move over the keys" },
		{ "A", "Type the key" },
		{ "B", "Delete a letter" },
		{ "X", "Capitals or small letters" },
		{ "Y", "Clear; Wi-Fi: show password" },
		{ "START", "Done" },
		{ NULL, NULL } } },
	{ "Other tools", {
		{ "Screenshots", "A hides labels, X deletes" },
		{ "Stats", "X changes the order, Y resets" },
		{ "Info", "A refresh, X Wi-Fi on/off" },
		{ "Clock", "SELECT 12 or 24 hour, Y syncs" },
		{ "Wi-Fi", "A connect, X forget/rescan" },
		{ NULL, NULL } } },
};
#define HELP_PAGES ((int)(sizeof(pages)/sizeof(pages[0])))

static void text(SDL_Surface* screen, TTF_Font* f, const char* s, SDL_Color color, int x, int y, int max_w) {
	if (!s || !*s) return;
	SDL_Surface* t = GFX_renderText(f, s, color);
	if (!t) return;
	SDL_BlitSurface(t, &(SDL_Rect){0,0,MIN(t->w, max_w),t->h}, screen, &(SDL_Rect){x, y});
	SDL_FreeSurface(t);
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();

	int row_h = SCALE1(PILL_SIZE);
	int top = SCALE1(PADDING) + row_h;
	int left = SCALE1(PADDING+BUTTON_PADDING);
	int key_w = screen->w * 3 / 10;

	int quit = 0, dirty = 1, show_setting = 0, page = 0;
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (PAD_justPressed(BTN_B)) quit = 1;
		else if (PAD_justRepeated(BTN_LEFT) || PAD_justRepeated(BTN_UP) || PAD_justRepeated(BTN_L1)) { page = (page+HELP_PAGES-1) % HELP_PAGES; dirty = 1; }
		else if (PAD_justRepeated(BTN_RIGHT) || PAD_justRepeated(BTN_DOWN) || PAD_justRepeated(BTN_R1)) { page = (page+1) % HELP_PAGES; dirty = 1; }

		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			char title[96], out[96];
			snprintf(title, sizeof(title), "%s  (%i / %i)", pages[page].title, page+1, HELP_PAGES);
			GFX_truncateText(font.large, title, out, screen->w - SCALE1(PADDING*2) - ow, SCALE1(BUTTON_PADDING*2));
			text(screen, font.large, out, COLOR_WHITE, left, SCALE1(PADDING)+4, screen->w);

			int y = top + SCALE1(4);
			for (int i=0; pages[page].rows[i].key; i++) {
				const Row* r = &pages[page].rows[i];
				text(screen, font.large, r->key, COLOR_WHITE, left, y, key_w);
				char does[160];
				GFX_truncateText(font.large, r->does, does, screen->w - left - key_w - SCALE1(PADDING), 0);
				text(screen, font.large, does, COLOR_LIGHT_TEXT, left + key_w, y, screen->w);
				y += row_h;
			}
			GFX_blitButtonGroup((char*[]){"LEFT/RIGHT","PAGE","B","BACK",NULL}, 1, screen, 1);
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
