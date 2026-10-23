#!/usr/bin/env python3
"""Turn a copy of workspace/macos/platform into the test platform: a fake SD card
path, rgb30 sized layout, software rendering (works with SDL's dummy video
driver) and scripted input plus screenshots driven by $HP_SCRIPT / $HP_OUT.
usage: platform-patch.py PLATFORM_DIR CARD_DIR"""
import re, sys

plat, card = sys.argv[1:3]

p = plat + '/platform.h'
s = open(p).read()
s = re.sub(r'#define SDCARD_PATH ".*"', '#define SDCARD_PATH "%s"' % card, s)
s = s.replace('#define FIXED_WIDTH\t\t640', '#define FIXED_WIDTH\t\t720').replace('#define FIXED_HEIGHT\t480', '#define FIXED_HEIGHT\t720')
s = s.replace('#define MAIN_ROW_COUNT 6', '#define MAIN_ROW_COUNT 8').replace('#define PADDING 10', '#define PADDING 40')
open(p, 'w').write(s)

p = plat + '/platform.c'
s = open(p).read()
assert 'SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC' in s
s = s.replace('SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC', 'SDL_RENDERER_SOFTWARE')
harness = r'''
///////////////////////////////
// test harness: scripted input from $HP_SCRIPT, screenshots into $HP_OUT

static void hp_snap(const char* name) {
	char path[512];
	snprintf(path, sizeof(path), "%s/%s.bmp", getenv("HP_OUT"), name);
	SDL_Surface* rgb = SDL_ConvertSurfaceFormat(vid.screen, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_SaveBMP(rgb, path);
	SDL_FreeSurface(rgb);
}
static void hp_press(int btn, int id, int down) {
	if (down) {
		pad.just_pressed |= btn; pad.just_repeated |= btn; pad.is_pressed |= btn;
		pad.repeat_at[id] = SDL_GetTicks() + 100000; // never auto-repeat
	}
	else {
		pad.is_pressed &= ~btn; pad.just_repeated &= ~btn; pad.just_released |= btn;
	}
}
void PLAT_pollInput(void) {
	pad.just_pressed = BTN_NONE;
	pad.just_released = BTN_NONE;
	pad.just_repeated = BTN_NONE;
	
	static int release_btn = 0, release_id = -1;
	static int wait = 20; // let the first frame draw
	static char* script = NULL;
	static char* cursor = NULL;
	if (!script) { script = strdup(getenv("HP_SCRIPT")); cursor = script; }
	
	if (release_btn) { hp_press(release_btn, release_id, 0); release_btn = 0; return; }
	if (wait-->0) return;
	if (!cursor || !*cursor) { fprintf(stderr, "script finished\n"); exit(0); }
	
	char tok[128]; int n = 0;
	while (*cursor && *cursor!=',' && n<127) tok[n++] = *cursor++;
	tok[n] = 0; if (*cursor==',') cursor++;
	wait = 12;
	
	if (!strncmp(tok,"snap:",5)) { hp_snap(tok+5); return; }
	int btn = BTN_NONE, id = -1;
	if (!strcmp(tok,"U")) { btn = BTN_DPAD_UP; id = BTN_ID_DPAD_UP; }
	else if (!strcmp(tok,"D")) { btn = BTN_DPAD_DOWN; id = BTN_ID_DPAD_DOWN; }
	else if (!strcmp(tok,"L")) { btn = BTN_DPAD_LEFT; id = BTN_ID_DPAD_LEFT; }
	else if (!strcmp(tok,"R")) { btn = BTN_DPAD_RIGHT; id = BTN_ID_DPAD_RIGHT; }
	else if (!strcmp(tok,"A")) { btn = BTN_A; id = BTN_ID_A; }
	else if (!strcmp(tok,"B")) { btn = BTN_B; id = BTN_ID_B; }
	else if (!strcmp(tok,"X")) { btn = BTN_X; id = BTN_ID_X; }
	else if (!strcmp(tok,"Y")) { btn = BTN_Y; id = BTN_ID_Y; }
	else if (!strcmp(tok,"wait")) return;
	if (btn) { hp_press(btn, id, 1); release_btn = btn; release_id = id; }
}
'''
marker = 'void PLAT_flip(SDL_Surface* IGNORED, int ignored) {'
assert s.count(marker) == 1
s = s.replace(marker, harness + '\n' + marker)
open(p, 'w').write(s)
