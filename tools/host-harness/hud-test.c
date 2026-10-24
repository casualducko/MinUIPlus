// Renders the in-game volume/brightness pill (GFX_renderSettingHud) over a stand-in for a
// game frame, the way the rgb30 platform composites it, and saves the result as a BMP.
// Built and run by run.sh-style setup: see the commands in README.md ("hud").
#include <stdio.h>
#include <stdlib.h>
#include "sdl.h"
#include "defines.h"
#include "api.h"
#include "utils.h"

int main(int argc, char** argv) {
	const char* out = argc>1 ? argv[1] : ".";
	SDL_Surface* screen = GFX_init(MODE_MENU);
	// a busy, light and dark background so any fringe or box around the pill would show
	SDL_Surface* frame = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h, 32, SDL_PIXELFORMAT_ARGB8888);
	for (int y=0; y<frame->h; y++) for (int x=0; x<frame->w; x++) {
		int v = ((x/40 + y/40) % 2) ? 230 : 40;
		if (x > frame->w/2) v = 255 - v;
		((uint32_t*)((uint8_t*)frame->pixels + y*frame->pitch))[x] = 0xFF000000 | v<<16 | (v*3/4)<<8 | v/2;
	}
	for (int kind=1; kind<=2; kind++) {
		SDL_Surface* hud = GFX_renderSettingHud(kind);
		if (!hud) { fprintf(stderr, "no hud\n"); return 1; }
		SDL_Surface* shot = SDL_ConvertSurface(frame, frame->format, 0);
		SDL_SetSurfaceBlendMode(hud, SDL_BLENDMODE_BLEND);
		SDL_BlitSurface(hud, NULL, shot, NULL);
		char path[512];
		snprintf(path, sizeof(path), "%s/hud-%s.bmp", out, kind==1?"brightness":"volume");
		SDL_SaveBMP(shot, path);
		SDL_FreeSurface(shot);
	}
	return 0;
}
