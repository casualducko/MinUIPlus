// Sleep, power-off, audio-buffer and low-battery settings. Writes .userdata/<platform>/sleep-timeout and
// poweroff-timeout (seconds, 0 for never), audio-buffer (milliseconds, 0 for the default) and
// low-battery-shutdown (percent, 0 for off);
// the launcher and the emulator read them when they start.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"

#define SLEEP_PATH USERDATA_PATH "/sleep-timeout"
#define POWEROFF_PATH USERDATA_PATH "/poweroff-timeout"
#define AUDIO_PATH USERDATA_PATH "/audio-buffer"
#define BATTERY_PATH USERDATA_PATH "/low-battery-shutdown"
#define RUMBLE_PATH USERDATA_PATH "/rumble"
#define ART_PATH SHOW_ART_PATH
#define CLOCK_PATH SHOW_CLOCK_PATH
#define NEW_PATH SHOW_NEW_PATH
#define AUTOSAVE_PATH AUTOSAVE_QUIT_PATH
#define AUTOSAVE_ROLL AUTOSAVE_ROLL_PATH
#define DEFAULT_SECONDS 120 // what the system uses when there is no file

typedef struct { int seconds; const char* label; } Choice;
static const Choice sleep_choices[] = {
	{0,"Never"}, {30,"30 seconds"}, {60,"1 minute"}, {120,"2 minutes"}, {300,"5 minutes"},
	{600,"10 minutes"}, {900,"15 minutes"}, {1800,"30 minutes"}, {-1,NULL}
};
static const Choice poweroff_choices[] = {
	{0,"Never"}, {30,"30 seconds"}, {60,"1 minute"}, {120,"2 minutes"}, {300,"5 minutes"},
	{600,"10 minutes"}, {1800,"30 minutes"}, {3600,"1 hour"}, {-1,NULL}
};

static const Choice audio_choices[] = {
	{0,"Default"}, {40,"40 ms"}, {60,"60 ms"}, {80,"80 ms"}, {120,"120 ms"}, {200,"200 ms"}, {-1,NULL}
};

static const Choice battery_choices[] = {
	{0,"Off"}, {3,"3%"}, {5,"5%"}, {8,"8%"}, {10,"10%"}, {-1,NULL}
};

static const Choice roll_choices[] = {
	{0,"Off"}, {3,"Every 3 min"}, {5,"Every 5 min"}, {10,"Every 10 min"}, {15,"Every 15 min"}, {-1,NULL}
};

static const Choice art_choices[] = {
	{1,"On"}, {0,"Off"}, {-1,NULL}
};

static const Choice auto_art_choices[] = {
	{0,"Manual"}, {1,"Automatic"}, {-1,NULL}
};

static const Choice clock_choices[] = {
	{0,"Off"}, {12,"12-hour"}, {24,"24-hour"}, {-1,NULL}
};

static const Choice rumble_choices[] = {
	{0,"Off"}, {25,"25%"}, {50,"50%"}, {75,"75%"}, {100,"Full"}, {-1,NULL}
};

typedef struct {
	const char* name;
	const char* path;
	const Choice* choices;
	int fallback;	// what the system uses when the file is missing or unreadable
	int min, max;	// accepted values besides 0 (and, for the timings, "10 seconds or more")
	int seconds;	// what is set now (milliseconds for the audio row)
	const char* note; // what the setting does, in one short line
} Row;
static Row rows[] = { // alphabetical
	{"Art for new games", TITLEART_AUTO_PATH, auto_art_choices, 0, 1, 1, 0, "Automatic: new games get a title picture at boot."},
	{"Audio buffer", AUDIO_PATH, audio_choices, 0, 20, 500, 0, "Bigger is safer against crackle, with a little more delay."},
	{"Auto-save on quit", AUTOSAVE_PATH, art_choices, 1, 1, 1, 1, "Quitting a game also saves a state, for the Switcher (Fn)."},
	{"Auto-save in game", AUTOSAVE_ROLL, roll_choices, 5, 1, 60, 5, "A crash or a dead battery then loses minutes, not the game (Switcher, Fn)."},
	{"Clock in corner", CLOCK_PATH, clock_choices, 0, 12, 24, 0, "Shows the time beside the Wi-Fi and battery icons."},
	{"Game art", ART_PATH, art_choices, 1, 1, 1, 1, "Off: only text in the launcher."},
	{"Low battery shutdown", BATTERY_PATH, battery_choices, 5, 1, 20, 5, "At this level the game is saved and the device powers off."},
	{"New game dot", NEW_PATH, art_choices, 1, 1, 1, 1, "A dot in the left margin beside games you have not played."},
	{"Power off after sleep", POWEROFF_PATH, poweroff_choices, DEFAULT_SECONDS, 10, 86400, DEFAULT_SECONDS, "How long it sleeps before it powers off."},
	{"Rumble", RUMBLE_PATH, rumble_choices, 100, 1, 100, 100, "The RGB20SX has no rumble motor (kept for units that have one)."},
	{"Sleep after", SLEEP_PATH, sleep_choices, DEFAULT_SECONDS, 10, 86400, DEFAULT_SECONDS, "Games don't sleep by themselves; power still does."},
};
#define ROW_COUNT ((int)(sizeof(rows)/sizeof(rows[0])))
#define VISIBLE_ROWS 7 // more rows than that scroll, so the notes and buttons keep their room (7 rows end at 520 px; the notes sit below, the buttons from 620)

static int readValue(const Row* row) { // same rules as the system: a bad file means the default
	char text[32] = "";
	getFile((char*)row->path, text, sizeof(text));
	if (!text[0]) return row->fallback;
	char* end = NULL;
	long n = strtol(text, &end, 10);
	while (end && (*end==' ' || *end=='\n' || *end=='\r')) end++;
	if (end==text || (end && *end) || n<0 || n>row->max || (n!=0 && n<row->min)) return row->fallback;
	if (row->choices==clock_choices && n!=0 && n!=12 && n!=24) return row->fallback; // (the launcher shows no clock for any other number)
	return (int)n;
}
static uint32_t rumble_off_at = 0; // when the short buzz after changing the rumble setting stops (0: not buzzing)
static int choiceIndex(const Row* row) { // -1 when the file holds something that isn't in the list
	for (int i=0; row->choices[i].label; i++) if (row->choices[i].seconds==row->seconds) return i;
	return -1;
}
static void describe(const Row* row, char* out, size_t cap) {
	int i = choiceIndex(row);
	if (i>=0) snprintf(out, cap, "%s", row->choices[i].label);
	else snprintf(out, cap, row->choices==audio_choices ? "%i ms" : row->choices==battery_choices || row->choices==rumble_choices ? "%i%%" : "%i seconds", row->seconds);
}
static void step(Row* row, int dir) {
	int count = 0;
	while (row->choices[count].label) count++;
	if (count==0) return; // (every row has choices; this only keeps the arithmetic below honest)
	int i = choiceIndex(row);
	if (i<0) i = dir>0 ? -1 : 0; // a custom value: go to the first (or last) choice
	i = (i + dir + count) % count;
	row->seconds = row->choices[i].seconds;
	char text[16];
	snprintf(text, sizeof(text), "%i\n", row->seconds);
	putFile((char*)row->path, text);
	if (row->choices==rumble_choices && row->seconds>0) { // so you can feel what you chose
		PLAT_setRumble(65535 * row->seconds / 100);
		rumble_off_at = SDL_GetTicks() + 300;
	}
}


static void drawRow(SDL_Surface* screen, int j, int selected, const Row* row) {
	int row_h = SCALE1(PILL_SIZE);
	int y = SCALE1(PADDING) + (j+1)*row_h;
	int w = screen->w - SCALE1(PADDING*2);
	SDL_Color color = COLOR_WHITE;
	if (selected) {
		GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), y, w, row_h});
		color = COLOR_BLACK;
	}
	SDL_Surface* name = TTF_RenderUTF8_Blended(font.large, row->name, color);
	if (name) {
		SDL_BlitSurface(name, NULL, screen, &(SDL_Rect){SCALE1(PADDING+BUTTON_PADDING), y+(row_h-name->h)/2});
		SDL_FreeSurface(name);
	}
	char value[48];
	describe(row, value, sizeof(value));
	SDL_Surface* v = TTF_RenderUTF8_Blended(font.large, value, selected ? COLOR_BLACK : COLOR_LIGHT_TEXT);
	if (v) {
		SDL_BlitSurface(v, NULL, screen, &(SDL_Rect){SCALE1(PADDING)+w-v->w-SCALE1(BUTTON_PADDING), y+(row_h-v->h)/2});
		SDL_FreeSurface(v);
	}
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();
	mkdirs(USERDATA_PATH);

	for (int i=0; i<ROW_COUNT; i++) rows[i].seconds = readValue(&rows[i]);
	SetRawRumble(0); // only to find out whether there is a motor to drive (it does not move it)

	int quit = 0, dirty = 1, show_setting = 0, sel = 0, first = 0;
	int visible = MIN(ROW_COUNT, VISIBLE_ROWS);
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);
		if (rumble_off_at && SDL_GetTicks()>=rumble_off_at) { PLAT_setRumble(0); rumble_off_at = 0; }

		if (PAD_justPressed(BTN_B)) quit = 1;
		else if (PAD_justRepeated(BTN_UP)) { sel = (sel+ROW_COUNT-1) % ROW_COUNT; dirty = 1; }
		else if (PAD_justRepeated(BTN_DOWN)) { sel = (sel+1) % ROW_COUNT; dirty = 1; }
		else if (PAD_justPressed(BTN_LEFT)) { step(&rows[sel], -1); dirty = 1; }
		else if (PAD_justPressed(BTN_RIGHT) || PAD_justPressed(BTN_A)) { step(&rows[sel], 1); dirty = 1; }

		if (sel<first) first = sel;
		if (sel>=first+visible) first = sel-visible+1;

		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			char title[64] = "Settings";
			char out[64];
			GFX_truncateText(font.large, title, out, screen->w - SCALE1(PADDING*2) - ow, SCALE1(BUTTON_PADDING*2));
			SDL_Surface* t = TTF_RenderUTF8_Blended(font.large, out, COLOR_LIGHT_TEXT);
			if (t) {
				SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){SCALE1(PADDING+BUTTON_PADDING), SCALE1(PADDING)+4});
				SDL_FreeSurface(t);
			}
			for (int i=0; i<visible; i++) drawRow(screen, i, first+i==sel, &rows[first+i]);
			if (ROW_COUNT>visible) {
				int sx = (screen->w - SCALE1(24))/2;
				int soy = SCALE1((PILL_SIZE - 4) / 2);
				if (first>0) GFX_blitAsset(ASSET_SCROLL_UP, NULL, screen, &(SDL_Rect){sx, SCALE1(PADDING) + soy});
				if (first+visible<ROW_COUNT) GFX_blitAsset(ASSET_SCROLL_DOWN, NULL, screen, &(SDL_Rect){sx, SCALE1(PADDING) + (visible+1)*SCALE1(PILL_SIZE) + SCALE1(3)});
			}
			// small text under the rows: what the selected setting does, then what applies to all of them
			{
				const char* notes[3] = { rows[sel].note, "Changes apply when a game or the launcher starts.", NULL };
				int y = SCALE1(PADDING) + (visible+1)*SCALE1(PILL_SIZE) + SCALE1(11); // below the arrow
				for (int i=0; notes[i]; i++) {
					SDL_Surface* n = TTF_RenderUTF8_Blended(font.small, notes[i], i==0 ? COLOR_LIGHT_TEXT : COLOR_GRAY);
					if (!n) continue;
					SDL_BlitSurface(n, &(SDL_Rect){0,0,MIN(n->w, screen->w-SCALE1(PADDING*2)),n->h}, screen, &(SDL_Rect){SCALE1(PADDING+BUTTON_PADDING), y});
					y += n->h + SCALE1(4);
					SDL_FreeSurface(n);
				}
			}
			GFX_blitButtonGroup((char*[]){"B","BACK","A","CHANGE",NULL}, 1, screen, 1);
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

	PLAT_setRumble(0);
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
