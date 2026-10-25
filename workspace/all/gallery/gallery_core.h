// The screenshot gallery's logic with no graphics (see gallery.c): what a screenshot's file name says,
// and which comes first. minarch names them "<game> YYYYMMDD-HHMMSS.png", with -2, -3 when two
// are taken in the same second.
#ifndef GALLERY_CORE_H
#define GALLERY_CORE_H
#include <stddef.h>

// "Zelda - Link 20261024-223105-2.png" -> game "Zelda - Link", when "2026-10-24 22:31:05". Returns 1 if the name
// carried a time stamp (otherwise game is the name without its extension and when is "").
int Shots_parse(const char* file, char* game, size_t gsize, char* when, size_t wsize);
// newest first (a file without a stamp goes after the others, by name); a qsort order for names
int Shots_compare(const char* a, const char* b);
int Shots_isImage(const char* file); // .png or .bmp, not hidden, not a .part left by an interrupted save

#endif
