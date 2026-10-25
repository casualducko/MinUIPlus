// System info: battery, Wi-Fi, storage, memory, play time, CPU, uptime, the build. A refreshes now,
// Up/Down scroll, L/R page, B quits. The numbers update by themselves every few seconds.
// The rows and their drawing live in info_view.c; the launcher's Fn page shows the same screen.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "info_view.h"

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();
	InfoView_gather();

	int visible = InfoView_visibleRows(screen);
	int quit = 0, dirty = 1, show_setting = 0, first = 0;
	uint32_t refreshed = SDL_GetTicks();
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		int row_count = InfoView_rowCount();
		if (PAD_justPressed(BTN_B)) quit = 1;
		else if (PAD_justRepeated(BTN_UP) && first>0) { first--; dirty = 1; }
		else if (PAD_justRepeated(BTN_DOWN) && first+visible<row_count) { first++; dirty = 1; }
		else if (PAD_justRepeated(BTN_LEFT) && first>0) { first = MAX(0, first-visible); dirty = 1; }
		else if (PAD_justRepeated(BTN_RIGHT) && first+visible<row_count) { first = MIN(row_count-visible, first+visible); dirty = 1; }
		else if (PAD_justPressed(BTN_A)) { InfoView_forceWifi(); InfoView_gather(); refreshed = SDL_GetTicks(); dirty = 1; }
		if (SDL_GetTicks() - refreshed > 3000) { InfoView_gather(); refreshed = SDL_GetTicks(); dirty = 1; }
		if (first+visible>row_count) first = MAX(0, row_count-visible);

		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			InfoView_draw(screen, first, ow);
			GFX_blitButtonGroup((char*[]){"B","BACK","A","REFRESH",NULL}, 1, screen, 1);
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
