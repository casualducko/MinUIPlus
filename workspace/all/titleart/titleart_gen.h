#ifndef TITLEART_GEN_H
#define TITLEART_GEN_H

#include <stddef.h>

#define TAGEN_MADE_PATH USERDATA_PATH "/titleart-made.txt" // mtime<TAB>path of every picture made here

int TAGen_open(void);   // the fonts; 0 if they opened (the other calls do it when needed)
void TAGen_close(void);
// the picture for `file` (a ROM's file name) with the system badge, saved at art_path (folders made);
// uses the theme's colors; 0 if the PNG is on disk
int TAGen_write(const char* file, const char* badge, const char* art_path);
int TAGen_artPath(const char* rom_path, char* out, size_t size); // <ROM folder>/.res/<file name>.png
void TAGen_record(const char* art_path); // remember that this tool made it (so it can be made again, never a hand-made one)
int TAGen_showArt(void); // the launcher's game art switch
// a ROM's picture unless it has one: 1 made, 0 it already had art, -1 failed; recorded
int TAGen_forRom(const char* rom_path);

#endif
