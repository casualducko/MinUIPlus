// Title art: the logic that needs no graphics (see titleart.c). A game's art is its title in big
// ASCII letters; everything here is a pure function of the file name and of how wide each line of
// the title turns out to be, so it can be tested on a computer.
#ifndef TITLEART_CORE_H
#define TITLEART_CORE_H

#include <stdint.h>

#define TA_COLS 60        // the tile is 60 x 30 cells of 6 x 12 px: 360 x 360
#define TA_ROWS 30
#define TA_LINE_MAX 128
#define TA_MAX_LINES 4

typedef struct {
	int h;                                  // letter height in rows
	int count;                              // lines of letters
	char lines[TA_MAX_LINES][TA_LINE_MAX];  // upper case
} TA_Layout;

void TA_title(const char* name, char* out, int size); // "Legend of Zelda, The (USA).sfc" -> "The Legend of Zelda"
void TA_split(const char* title, char* main, int msize, char* sub, int ssize); // "A - B" or "A: B" -> A and B ("" if none)
int TA_wrapWords(const char* text, int n, char lines[][TA_LINE_MAX]); // n lines as even as the words allow; 0 if there are fewer words
int TA_wrapPlain(const char* text, int width, int max_lines, char lines[][TA_LINE_MAX]); // greedy, the last line cut with ~
// aspect(line, ctx) is width / height of that line in big letters. Fills `out` and returns 1 when
// the whole title fits on up to four lines with letters at least 4 rows high; returns 0 when it does
// not (nothing is cut: the caller shows the title in plain text instead).
int TA_layout(const char* text, int max_cols, int max_rows, float (*aspect)(const char*, void*), void* ctx, TA_Layout* out);
// px: w x (2*rows) coverage; out: rows x w characters ('H' both halves, ' top half, . bottom half, space none)
void TA_pack(const uint8_t* px, int w, int rows, char* out);
int TA_badge(const char* folder, char* out, int size); // "Genesis (MD)" -> "GENESIS", a long name gives its tag

#endif
