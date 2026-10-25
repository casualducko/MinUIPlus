#ifndef INFO_VIEW_H
#define INFO_VIEW_H
#include "sdl.h"

// the System Info rows and their drawing, used by Tools > Info and the launcher's Fn page
void InfoView_gather(void);                  // read everything again (Wi-Fi at most every few seconds)
void InfoView_forceWifi(void);               // the next gather asks about Wi-Fi too
int  InfoView_rowCount(void);
int  InfoView_visibleRows(SDL_Surface* screen);
void InfoView_draw(SDL_Surface* screen, int first, int ow); // ow: width of the hardware group at the top right

#endif
