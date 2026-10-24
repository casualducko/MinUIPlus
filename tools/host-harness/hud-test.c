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
	for (int kind=1; kind<=3; kind++) {
		SDL_Surface* hud = kind==3 ? GFX_renderToastHud("Screenshot saved") : GFX_renderSettingHud(kind);
		if (!hud) { fprintf(stderr, "no hud\n"); return 1; }
		SDL_Surface* shot = SDL_ConvertSurface(frame, frame->format, 0);
		SDL_SetSurfaceBlendMode(hud, SDL_BLENDMODE_BLEND);
		SDL_BlitSurface(hud, NULL, shot, NULL);
		char path[512];
		snprintf(path, sizeof(path), "%s/hud-%s.bmp", out, kind==1?"brightness":kind==2?"volume":"toast");
		SDL_SaveBMP(shot, path);
		SDL_FreeSurface(shot);
	}
	// the save/load slot picker, laid out like the preview window in minarch's menu
	{
		int hw = screen->w / 2, hh = screen->h / 2;
		int pw = hw + SCALE1(8), ph = hh + SCALE1(8 + SLOT_PICKER_HEIGHT + 4);
		int ox = screen->w - pw - SCALE1(PADDING), oy = (screen->h - ph) / 2;
		for (int variant=0; variant<2; variant++) {
			GFX_clear(screen);
			int window_x = ox;
			GFX_blitRect(ASSET_STATE_BG, screen, &(SDL_Rect){ox,oy,pw,ph});
			SDL_Rect preview = {ox+SCALE1(4), oy+SCALE1(4), hw, hh};
			SDL_FillRect(screen, &preview, 0);
			int used[8] = {1,0,1,1,0,0,0,1};
			int none[8] = {0,0,0,0,0,0,0,0};
			GFX_blitSlotPicker(screen, window_x, oy+SCALE1(4)+hh+SCALE1(4), pw, variant ? 4 : 0, 8, variant ? none : used);
			char path[512];
			snprintf(path, sizeof(path), "%s/slots-%s.bmp", out, variant ? "slot5-all-empty" : "slot1");
			SDL_SaveBMP(screen, path);
		}
	}
	// the in-game Options list as minarch draws it (MENU_LIST: the main menu's big rows)
	{
		const char* names[] = {"Frontend","Emulator","Controls","Shortcuts","Save Changes","Restore Save",NULL};
		GFX_clear(screen);
		GFX_blitHardwareGroup(screen, 0);
		int ox = SCALE1(PADDING), oy = SCALE1(PADDING + PILL_SIZE);
		for (int i=0; names[i]; i++) {
			SDL_Color color = COLOR_WHITE;
			if (i==1) {
				int w = 0;
				TTF_SizeUTF8(font.large, names[i], &w, NULL);
				w += SCALE1(BUTTON_PADDING*2);
				GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){ox, oy+SCALE1(i*PILL_SIZE), w, SCALE1(PILL_SIZE)});
				color = COLOR_BLACK;
			}
			SDL_Surface* t = TTF_RenderUTF8_Blended(font.large, names[i], color);
			SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){ox+SCALE1(BUTTON_PADDING), oy+SCALE1(i*PILL_SIZE)+4});
			SDL_FreeSurface(t);
		}
		char path[512];
		snprintf(path, sizeof(path), "%s/options-list.bmp", out);
		SDL_SaveBMP(screen, path);
	}
	return 0;
}
