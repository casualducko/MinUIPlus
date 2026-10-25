// theme.cfg parsing (THEME_parseColor / THEME_parse in api.c)
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef struct { uint8_t r,g,b,a; } SDL_Color;
#define TRIAD_WHITE 0xff,0xff,0xff
#define TRIAD_BLACK 0x00,0x00,0x00
#define TRIAD_LIGHT_GRAY 0x7f,0x7f,0x7f
#define TRIAD_GRAY 0x99,0x99,0x99
#define TRIAD_DARK_GRAY 0x26,0x26,0x26
#define TRIAD_LIGHT_TEXT 0xcc,0xcc,0xcc
#define TRIAD_DARK_TEXT 0x66,0x66,0x66
#include "gen/theme_types.inc"
#include "gen/theme.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static int parse(const char* text, ThemeColors* t, char* font) {
	char buf[2048]; snprintf(buf, sizeof(buf), "%s", text);
	return THEME_parse(buf, t, font, 64);
}
int main(void) {
	SDL_Color c = {1,2,3,0};
	CHECK(THEME_parseColor("ff8000", &c) && c.r==255 && c.g==128 && c.b==0, "plain hex");
	CHECK(THEME_parseColor("#FF8000\n", &c) && c.r==255, "# and capitals and newline");
	CHECK(THEME_parseColor("0x102030", &c) && c.g==0x20 && c.b==0x30, "0x prefix");
	c = (SDL_Color){1,2,3,0};
	CHECK(!THEME_parseColor("ff80", &c) && c.r==1, "too short: unchanged");
	CHECK(!THEME_parseColor("ff80001", &c) && c.r==1, "too long: unchanged");
	CHECK(!THEME_parseColor("gg0000", &c) && !THEME_parseColor("", &c) && !THEME_parseColor(NULL, &c), "junk, empty, NULL");
	CHECK(!THEME_parseColor("ff0000 red", &c), "trailing words");

	ThemeColors t = theme; char font[64] = "";
	int n = parse("# my theme\nwhite = #ffcc00\nblack=000011\r\n  gray=0x808080  \nbogus=123456\nlight_text=zzz\nfont=Mono.ttf\n\nno equals line\n", &t, font);
	CHECK(n==4, "4 settings used (3 colors and the font)");
	CHECK(t.white.r==0xff && t.white.g==0xcc && t.white.b==0, "white set (spaces around =)");
	CHECK(t.black.b==0x11, "black set (CRLF line)");
	CHECK(t.gray.r==0x80, "gray set (indented, trailing spaces)");
	CHECK(t.light_text.r==0xcc, "bad value leaves the default");
	CHECK(!strcmp(font, "Mono.ttf"), "font");
	CHECK(t.dark_gray.r==0x26 && t.button_text.r==0x99, "unmentioned colors keep the defaults");
	CHECK(theme.white.r==0xff && theme.white.g==0xff, "the defaults themselves are untouched");
	t = theme; font[0] = 0;
	CHECK(parse("", &t, font)==0 && parse("\n\n", &t, font)==0, "empty file");
	char longfont[200]; memset(longfont, 'a', 150); longfont[150] = 0;
	char big[400]; snprintf(big, sizeof(big), "font=%s\n", longfont);
	CHECK(parse(big, &t, font)==0 && !font[0], "font name too long for the buffer is ignored");
	CHECK(parse("font=/abs/x.otf\n", &t, font)==1 && !strcmp(font, "/abs/x.otf"), "absolute font path");
	CHECK(parse("white=ffffff\n", &t, NULL)==1, "no font buffer is fine");
	return fails ? 1 : 0;
}
