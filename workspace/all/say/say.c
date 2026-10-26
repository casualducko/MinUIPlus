#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"

int main(int argc , char* argv[]) {
	
	char msg[1024];
	// say.elf --live <file>: shows what is in the file (read again every half second, with the seconds that have
	// passed), no button; the caller ends it (killall say.elf). For things that take a while, so it doesn't look frozen.
	const char* live = (argc>2 && !strcmp(argv[1], "--live")) ? argv[2] : NULL;
	char shown[1024] = "";
	if (live) snprintf(msg, sizeof(msg), "...");
	else snprintf(msg, sizeof(msg), "%s", argv[1]);
	
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();
	
	int quit = 0;
	
	int dirty = 1;
	uint32_t started = SDL_GetTicks(), checked = 0;
	while(!quit) {
		
		PAD_poll();
		if (!live && (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B))) {
			quit = 1;
		}
		if (live && SDL_GetTicks()-checked>=500) {
			checked = SDL_GetTicks();
			char text[900] = "";
			FILE* f = fopen(live, "r");
			if (f) { size_t n = fread(text, 1, sizeof(text)-1, f); text[n] = '\0'; fclose(f); }
			while (*text && text[strlen(text)-1]=='\n') text[strlen(text)-1] = '\0';
			snprintf(msg, sizeof(msg), "%s\n\n%us", text[0] ? text : "...", (unsigned)((checked-started)/1000));
			if (strcmp(msg, shown)) { snprintf(shown, sizeof(shown), "%s", msg); dirty = 1; }
		}
		
		// PWR_update(&dirty, NULL, NULL,NULL);
		
		if (dirty) {
			GFX_clearUI(screen);
			
			GFX_blitMessage(font.large, msg, screen,&(SDL_Rect){0,0,screen->w,screen->h-SCALE1(PADDING + PILL_SIZE + PADDING)});
			if (!live) GFX_blitButtonGroup((char*[]){ "A","OKAY", NULL }, 1, screen, 1);
			
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
