#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"

///////////////////////////////

void LOG_note(int level, const char* fmt, ...) {
	char buf[1024] = {0};
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	switch(level) {
#ifdef DEBUG
	case LOG_DEBUG:
		printf("[DEBUG] %s", buf);
		break;
#endif
	case LOG_INFO:
		printf("[INFO] %s", buf);
		break;
	case LOG_WARN:
		fprintf(stderr, "[WARN] %s", buf);
		break;
	case LOG_ERROR:
		fprintf(stderr, "[ERROR] %s", buf);
		break;
	default:
		break;
	}
	fflush(stdout);
}

///////////////////////////////

uint32_t RGB_WHITE;
static TTF_Font* fallback_fonts[4]; // the second font (see GFX_renderText), at the size of font.large, medium, small, tiny
uint32_t RGB_BACKGROUND;
uint32_t RGB_BLACK;
uint32_t RGB_LIGHT_GRAY;
uint32_t RGB_GRAY;
uint32_t RGB_DARK_GRAY;

static struct GFX_Context {
	SDL_Surface* screen;
	SDL_Surface* assets;

	int mode;
	int vsync;
} gfx;

static SDL_Rect asset_rects[ASSET_COUNT];
static uint32_t asset_rgbs[ASSET_COLORS];
GFX_Fonts font;

///////////////////////////////

static struct PWR_Context {
	int initialized;
	
	int can_sleep;
	int can_poweroff;
	int can_autosleep;
	int requested_sleep;
	int requested_wake;
	
	pthread_t battery_pt;
	int is_charging;
	int charge;
	int should_warn;
	int low_battery; // powering off because the battery is nearly flat (changes the message)

	SDL_Surface* overlay;
} pwr = {0};

///////////////////////////////

// The theme file, .userdata/<platform>/theme.cfg: lines of name=RRGGBB (a leading # or 0x is fine)
// for white, black, light_gray, gray, dark_gray, light_text, dark_text and button_text, and
// font=<file> (a .ttf or .otf in the same folder, or an absolute path). Anything else is ignored.
ThemeColors theme = {
	{TRIAD_WHITE}, {TRIAD_BLACK}, {TRIAD_LIGHT_GRAY}, {TRIAD_GRAY}, {TRIAD_DARK_GRAY},
	{TRIAD_LIGHT_TEXT}, {TRIAD_DARK_TEXT}, {TRIAD_GRAY},
	{TRIAD_BLACK},
};
int THEME_parseColor(const char* s, SDL_Color* out) { // 1 if s is a color
	if (!s) return 0;
	if (*s=='#') s++;
	else if (s[0]=='0' && (s[1]=='x' || s[1]=='X')) s += 2;
	unsigned v = 0;
	int n = 0;
	for (; n<7 && *s; s++, n++) {
		char c = *s;
		int d = c>='0'&&c<='9' ? c-'0' : c>='a'&&c<='f' ? c-'a'+10 : c>='A'&&c<='F' ? c-'A'+10 : -1;
		if (d<0) break;
		v = v*16 + d;
	}
	while (*s==' ' || *s=='\t' || *s=='\r' || *s=='\n') s++;
	if (n!=6 || *s) return 0;
	out->r = (v>>16) & 0xff;
	out->g = (v>>8) & 0xff;
	out->b = v & 0xff;
	return 1;
}
// applies the file's text (which it edits) to t; font gets the font= value; returns how many lines were used
int THEME_parse(char* text, ThemeColors* t, char* font, int font_size) {
	struct { const char* name; SDL_Color* color; } names[] = {
		{"white", &t->white}, {"black", &t->black}, {"light_gray", &t->light_gray},
		{"gray", &t->gray}, {"dark_gray", &t->dark_gray}, {"light_text", &t->light_text},
		{"dark_text", &t->dark_text}, {"button_text", &t->button_text}, {"background", &t->background},
	};
	int used = 0;
	char* save = NULL;
	for (char* line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		while (*line==' ' || *line=='\t') line++;
		if (*line=='#' || !*line) continue;
		char* eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		char* key = line;
		char* value = eq + 1;
		char* end = key + strlen(key);
		while (end>key && (end[-1]==' ' || end[-1]=='\t')) *--end = '\0';
		while (*value==' ' || *value=='\t') value++;
		end = value + strlen(value);
		while (end>value && (end[-1]==' ' || end[-1]=='\t' || end[-1]=='\r')) *--end = '\0';
		if (!strcmp(key, "font")) {
			if (*value && font && (int)strlen(value)<font_size) { strcpy(font, value); used++; }
			continue;
		}
		for (int i=0; i<(int)(sizeof(names)/sizeof(names[0])); i++) {
			if (!strcmp(key, names[i].name) && THEME_parseColor(value, names[i].color)) { used++; break; }
		}
	}
	return used;
}
// the file Tools > Theme writes: every color, and the font if there is one
int THEME_format(const ThemeColors* t, const char* font, char* out, int size) {
	const struct { const char* name; const SDL_Color* color; } names[] = {
		{"background", &t->background}, {"white", &t->white}, {"black", &t->black}, {"light_gray", &t->light_gray},
		{"gray", &t->gray}, {"dark_gray", &t->dark_gray}, {"light_text", &t->light_text},
		{"dark_text", &t->dark_text}, {"button_text", &t->button_text},
	};
	int n = snprintf(out, size, "# Written by Tools > Theme. Colors are RRGGBB. Delete this file for the stock look.\n");
	for (int i=0; i<(int)(sizeof(names)/sizeof(names[0])) && n>=0 && n<size; i++) {
		n += snprintf(out+n, size-n, "%s=%02x%02x%02x\n", names[i].name, names[i].color->r, names[i].color->g, names[i].color->b);
	}
	if (font && *font && n>=0 && n<size) n += snprintf(out+n, size-n, "font=%s\n", font);
	if (n>=size) { out[size-1] = '\0'; return size-1; }
	return n;
}
// end of the theme file code

// The flat-colored assets (pill ends, bars, dots...) are drawn from the sheet, not filled, so a theme
// has to recolor them there: every visible pixel of the asset takes the new color, keeping its alpha.
static void GFX_tintAssets(void) {
	if (!gfx.assets || gfx.assets->format->BitsPerPixel!=32) return;
	static const SDL_Color defaults[ASSET_COLORS] = {
		[ASSET_WHITE_PILL]={TRIAD_WHITE}, [ASSET_BLACK_PILL]={TRIAD_BLACK}, [ASSET_DARK_GRAY_PILL]={TRIAD_DARK_GRAY},
		[ASSET_OPTION]={TRIAD_DARK_GRAY}, [ASSET_BUTTON]={TRIAD_WHITE}, [ASSET_PAGE_BG]={TRIAD_WHITE},
		[ASSET_STATE_BG]={TRIAD_WHITE}, [ASSET_PAGE]={TRIAD_BLACK}, [ASSET_BAR]={TRIAD_WHITE},
		[ASSET_BAR_BG]={TRIAD_BLACK}, [ASSET_BAR_BG_MENU]={TRIAD_DARK_GRAY}, [ASSET_UNDERLINE]={TRIAD_GRAY},
		[ASSET_DOT]={TRIAD_LIGHT_GRAY}, [ASSET_HOLE]={TRIAD_BLACK},
	};
	SDL_Surface* sheet = gfx.assets;
	SDL_PixelFormat* fmt = sheet->format;
	for (int i=0; i<ASSET_COLORS; i++) {
		uint8_t r,g,b;
		SDL_GetRGB(asset_rgbs[i], gfx.screen->format, &r, &g, &b);
		if (r==defaults[i].r && g==defaults[i].g && b==defaults[i].b) continue; // unchanged
		SDL_Rect rect = asset_rects[i];
		for (int y=rect.y; y<rect.y+rect.h && y<sheet->h; y++) {
			uint32_t* row = (uint32_t*)((uint8_t*)sheet->pixels + y*sheet->pitch);
			for (int x=rect.x; x<rect.x+rect.w && x<sheet->w; x++) {
				uint8_t pr,pg,pb,pa;
				SDL_GetRGBA(row[x], fmt, &pr, &pg, &pb, &pa);
				if (pa) row[x] = SDL_MapRGBA(fmt, r, g, b, pa);
			}
		}
	}
	// the corner icons (volume, brightness, battery, Wi-Fi) are white on the sheet: they follow the text color.
	// The low-battery red keeps its own color. (The scroll arrows are tinted below, always.)
	if (theme.white.r!=0xff || theme.white.g!=0xff || theme.white.b!=0xff) {
		static const int icons[] = {ASSET_BRIGHTNESS, ASSET_VOLUME_MUTE, ASSET_VOLUME, ASSET_BATTERY, ASSET_BATTERY_FILL, ASSET_BATTERY_BOLT, ASSET_WIFI};
		for (int i=0; i<(int)(sizeof(icons)/sizeof(icons[0])); i++) {
			SDL_Rect rect = asset_rects[icons[i]];
			for (int y=rect.y; y<rect.y+rect.h && y<sheet->h; y++) {
				uint32_t* row = (uint32_t*)((uint8_t*)sheet->pixels + y*sheet->pitch);
				for (int x=rect.x; x<rect.x+rect.w && x<sheet->w; x++) {
					uint8_t pr,pg,pb,pa;
					SDL_GetRGBA(row[x], fmt, &pr, &pg, &pb, &pa);
					if (pa) row[x] = SDL_MapRGBA(fmt, theme.white.r, theme.white.g, theme.white.b, pa);
				}
			}
		}
	}
	// the scroll arrows (more rows above or below) are the UI's white, not the sheet's gray
	static const int arrows[] = {ASSET_SCROLL_UP, ASSET_SCROLL_DOWN};
	for (int i=0; i<2; i++) {
		SDL_Rect rect = asset_rects[arrows[i]];
		for (int y=rect.y; y<rect.y+rect.h && y<sheet->h; y++) {
			uint32_t* row = (uint32_t*)((uint8_t*)sheet->pixels + y*sheet->pitch);
			for (int x=rect.x; x<rect.x+rect.w && x<sheet->w; x++) {
				uint8_t pr,pg,pb,pa;
				SDL_GetRGBA(row[x], fmt, &pr, &pg, &pb, &pa);
				if (pa) row[x] = SDL_MapRGBA(fmt, theme.white.r, theme.white.g, theme.white.b, pa);
			}
		}
	}
}

static void GFX_mapTheme(void) { // theme -> the colors in use, and the ones each asset gets
	RGB_BACKGROUND	= SDL_MapRGB(gfx.screen->format, theme.background.r, theme.background.g, theme.background.b);
	RGB_WHITE		= SDL_MapRGB(gfx.screen->format, theme.white.r, theme.white.g, theme.white.b);
	RGB_BLACK		= SDL_MapRGB(gfx.screen->format, theme.black.r, theme.black.g, theme.black.b);
	RGB_LIGHT_GRAY	= SDL_MapRGB(gfx.screen->format, theme.light_gray.r, theme.light_gray.g, theme.light_gray.b);
	RGB_GRAY		= SDL_MapRGB(gfx.screen->format, theme.gray.r, theme.gray.g, theme.gray.b);
	RGB_DARK_GRAY	= SDL_MapRGB(gfx.screen->format, theme.dark_gray.r, theme.dark_gray.g, theme.dark_gray.b);
	
	asset_rgbs[ASSET_WHITE_PILL]	= RGB_WHITE;
	asset_rgbs[ASSET_BLACK_PILL]	= RGB_BLACK;
	asset_rgbs[ASSET_DARK_GRAY_PILL]= RGB_DARK_GRAY;
	asset_rgbs[ASSET_OPTION]		= RGB_DARK_GRAY;
	asset_rgbs[ASSET_BUTTON]		= RGB_WHITE;
	asset_rgbs[ASSET_PAGE_BG]		= RGB_WHITE;
	asset_rgbs[ASSET_STATE_BG]		= RGB_WHITE;
	asset_rgbs[ASSET_PAGE]			= RGB_BLACK;
	asset_rgbs[ASSET_BAR]			= RGB_WHITE;
	asset_rgbs[ASSET_BAR_BG]		= RGB_BLACK;
	asset_rgbs[ASSET_BAR_BG_MENU]	= RGB_DARK_GRAY;
	asset_rgbs[ASSET_UNDERLINE]		= RGB_GRAY;
	asset_rgbs[ASSET_DOT]			= RGB_LIGHT_GRAY;
	asset_rgbs[ASSET_HOLE]			= RGB_BLACK;
}
// after `theme` has been changed (the Theme tool does): the colors again and a freshly recolored sheet
void GFX_applyTheme(void) {
	GFX_mapTheme();
	char asset_path[MAX_PATH];
	sprintf(asset_path, RES_PATH "/assets@%ix.png", FIXED_SCALE);
	SDL_Surface* fresh = IMG_Load(asset_path);
	if (!fresh) return;
	SDL_FreeSurface(gfx.assets);
	gfx.assets = fresh;
	GFX_tintAssets();
}
SDL_Surface* GFX_init(int mode) {
	// TODO: this doesn't really belong here...
	// tried adding to PWR_init() but that was no good (not sure why)
	PLAT_initLid();
	
	gfx.screen = PLAT_initVideo();
	gfx.vsync = VSYNC_STRICT;
	gfx.mode = mode;
	
	char theme_font[MAX_PATH] = "";
	{
		char text[2048] = "";
		FILE* f = fopen(USERDATA_PATH "/theme.cfg", "r");
		if (f) {
			size_t n = fread(text, 1, sizeof(text)-1, f);
			text[n] = '\0';
			fclose(f);
			LOG_info("theme.cfg: %i settings used\n", THEME_parse(text, &theme, theme_font, sizeof(theme_font)));
		}
	}
	GFX_mapTheme();
	
	asset_rects[ASSET_WHITE_PILL]		= (SDL_Rect){SCALE4( 1, 1,30,30)};
	asset_rects[ASSET_BLACK_PILL]		= (SDL_Rect){SCALE4(33, 1,30,30)};
	asset_rects[ASSET_DARK_GRAY_PILL]	= (SDL_Rect){SCALE4(65, 1,30,30)};
	asset_rects[ASSET_OPTION]			= (SDL_Rect){SCALE4(97, 1,20,20)};
	asset_rects[ASSET_BUTTON]			= (SDL_Rect){SCALE4( 1,33,20,20)};
	asset_rects[ASSET_PAGE_BG]			= (SDL_Rect){SCALE4(64,33,15,15)};
	asset_rects[ASSET_STATE_BG]			= (SDL_Rect){SCALE4(23,54, 8, 8)};
	asset_rects[ASSET_PAGE]				= (SDL_Rect){SCALE4(39,54, 6, 6)};
	asset_rects[ASSET_BAR]				= (SDL_Rect){SCALE4(33,58, 4, 4)};
	asset_rects[ASSET_BAR_BG]			= (SDL_Rect){SCALE4(15,55, 4, 4)};
	asset_rects[ASSET_BAR_BG_MENU]		= (SDL_Rect){SCALE4(85,56, 4, 4)};
	asset_rects[ASSET_UNDERLINE]		= (SDL_Rect){SCALE4(85,51, 3, 3)};
	asset_rects[ASSET_DOT]				= (SDL_Rect){SCALE4(33,54, 2, 2)};
	asset_rects[ASSET_BRIGHTNESS]		= (SDL_Rect){SCALE4(23,33,19,19)};
	asset_rects[ASSET_VOLUME_MUTE]		= (SDL_Rect){SCALE4(44,33,10,16)};
	asset_rects[ASSET_VOLUME]			= (SDL_Rect){SCALE4(44,33,18,16)};
	asset_rects[ASSET_BATTERY]			= (SDL_Rect){SCALE4(47,51,17,10)};
	asset_rects[ASSET_BATTERY_LOW]		= (SDL_Rect){SCALE4(66,51,17,10)};
	asset_rects[ASSET_BATTERY_FILL]		= (SDL_Rect){SCALE4(81,33,12, 6)};
	asset_rects[ASSET_BATTERY_FILL_LOW]	= (SDL_Rect){SCALE4( 1,55,12, 6)};
	asset_rects[ASSET_BATTERY_BOLT]		= (SDL_Rect){SCALE4(81,41,12, 6)};
	asset_rects[ASSET_SCROLL_UP]		= (SDL_Rect){SCALE4(97,23,24, 6)};
	asset_rects[ASSET_SCROLL_DOWN]		= (SDL_Rect){SCALE4(97,31,24, 6)};
	asset_rects[ASSET_WIFI]				= (SDL_Rect){SCALE4(95,39,14,10)};
	asset_rects[ASSET_HOLE]				= (SDL_Rect){SCALE4( 1,63,20,20)};
	
	char asset_path[MAX_PATH];
	sprintf(asset_path, RES_PATH "/assets@%ix.png", FIXED_SCALE);
	if (!exists(asset_path)) LOG_info("missing assets, you're about to segfault dummy!\n");
	gfx.assets = IMG_Load(asset_path);
	GFX_tintAssets();
	
	TTF_Init();
	const char* font_path = FONT_PATH;
	char custom_font[MAX_PATH] = "";
	if (*theme_font) { // a font from the theme file, if it opens
		if (theme_font[0]=='/') snprintf(custom_font, sizeof(custom_font), "%s", theme_font);
		else snprintf(custom_font, sizeof(custom_font), USERDATA_PATH "/%s", theme_font);
		TTF_Font* probe = TTF_OpenFont(custom_font, SCALE1(FONT_LARGE));
		if (probe) { TTF_CloseFont(probe); font_path = custom_font; }
		else LOG_info("theme.cfg: can't open font %s, using the default\n", custom_font);
	}
	font.large 	= TTF_OpenFont(font_path, SCALE1(FONT_LARGE));
	font.medium = TTF_OpenFont(font_path, SCALE1(FONT_MEDIUM));
	font.small 	= TTF_OpenFont(font_path, SCALE1(FONT_SMALL));
	font.tiny 	= TTF_OpenFont(font_path, SCALE1(FONT_TINY));
	
	if (exists(FALLBACK_FONT_PATH)) { // for the characters the UI font lacks (see GFX_renderText)
		fallback_fonts[0] = TTF_OpenFont(FALLBACK_FONT_PATH, SCALE1(FONT_LARGE));
		fallback_fonts[1] = TTF_OpenFont(FALLBACK_FONT_PATH, SCALE1(FONT_MEDIUM));
		fallback_fonts[2] = TTF_OpenFont(FALLBACK_FONT_PATH, SCALE1(FONT_SMALL));
		fallback_fonts[3] = TTF_OpenFont(FALLBACK_FONT_PATH, SCALE1(FONT_TINY));
	}
	TTF_SetFontStyle(font.large, TTF_STYLE_BOLD);
	TTF_SetFontStyle(font.medium, TTF_STYLE_BOLD);
	TTF_SetFontStyle(font.small, TTF_STYLE_BOLD);
	TTF_SetFontStyle(font.tiny, TTF_STYLE_BOLD);
	
	return gfx.screen;
}
void GFX_quit(void) {
	for (int i=0; i<4; i++) { if (fallback_fonts[i]) TTF_CloseFont(fallback_fonts[i]); fallback_fonts[i] = NULL; }
	TTF_CloseFont(font.large);
	TTF_CloseFont(font.medium);
	TTF_CloseFont(font.small);
	TTF_CloseFont(font.tiny);
	
	SDL_FreeSurface(gfx.assets);
	
	GFX_freeAAScaler();
	
	GFX_clearAll();

	PLAT_quitVideo();
}

void GFX_clearUI(SDL_Surface* screen) {
	SDL_FillRect(screen, NULL, RGB_BACKGROUND);
}
void GFX_setMode(int mode) {
	gfx.mode = mode;
}
int GFX_getVsync(void) {
	return gfx.vsync;
}
void GFX_setVsync(int vsync) {
	PLAT_setVsync(vsync);
	gfx.vsync = vsync;
}

int GFX_hdmiChanged(void) {
	static int had_hdmi = -1;
	int has_hdmi = GetHDMI();
	if (had_hdmi==-1) had_hdmi = has_hdmi;
	if (had_hdmi==has_hdmi) return 0;
	had_hdmi = has_hdmi;
	return 1;
}

#define FRAME_BUDGET 17 // 60fps
static uint32_t frame_start = 0;
void GFX_startFrame(void) {
	frame_start = SDL_GetTicks();
}

void GFX_flip(SDL_Surface* screen) {
	int should_vsync = (gfx.vsync!=VSYNC_OFF && (gfx.vsync==VSYNC_STRICT || frame_start==0 || SDL_GetTicks()-frame_start<FRAME_BUDGET));
	PLAT_flip(screen, should_vsync);
}
void GFX_sync(void) {
	uint32_t frame_duration = SDL_GetTicks() - frame_start;
	if (gfx.vsync!=VSYNC_OFF) {
		// this limiting condition helps SuperFX chip games
		if (gfx.vsync==VSYNC_STRICT || frame_start==0 || frame_duration<FRAME_BUDGET) { // only wait if we're under frame budget
			PLAT_vsync(FRAME_BUDGET-frame_duration);
		}
	}
	else {
		if (frame_duration<FRAME_BUDGET) SDL_Delay(FRAME_BUDGET-frame_duration);
	}
}

FALLBACK_IMPLEMENTATION int PLAT_supportsOverscan(void) { return 0; }
FALLBACK_IMPLEMENTATION void PLAT_setEffectColor(int next_color) { }

// Text with a second font behind the first: the UI font has no Japanese (or Chinese, or Korean), so a
// game with such a title was blank. Each stretch of characters the font lacks is drawn from
// .system/res/fallback.ttf (Noto Sans JP, subset) at the same size, and measured the same way, so
// truncating to a width still works. Without that file everything is as before.
TTF_Font* GFX_fallbackFont(TTF_Font* f) {
	if (f==font.large) return fallback_fonts[0];
	if (f==font.medium) return fallback_fonts[1];
	if (f==font.small) return fallback_fonts[2];
	if (f==font.tiny) return fallback_fonts[3];
	return NULL;
}
static int GFX_glyphIn(TTF_Font* f, uint32_t cp) {
	if (cp<0x80) return 1; // the plain letters are in the UI font
	return cp<=0xFFFF && TTF_GlyphIsProvided(f, (Uint16)cp);
}
#define TEXT_MAX_RUNS 64
typedef struct { int from, len; TTF_Font* font; } TextRun;
// the runs of text that one font can draw: the first font where it has the character, else the second
// (a character neither has goes to the first, which draws its blank box); at most TEXT_MAX_RUNS
static int GFX_textRuns(TTF_Font* primary, TTF_Font* fb, const char* text, TextRun* runs) {
	int count = 0;
	for (int i=0; text[i]; ) {
		uint32_t cp;
		int n = UTF8_next(text+i, &cp);
		if (n<=0) break;
		TTF_Font* use = (GFX_glyphIn(primary, cp) || !GFX_glyphIn(fb, cp)) ? primary : fb;
		if (count>0 && (runs[count-1].font==use || count>=TEXT_MAX_RUNS)) runs[count-1].len += n; // (the last run takes the rest of a very mixed text)
		else { runs[count].from = i; runs[count].len = n; runs[count].font = use; count++; }
		i += n;
	}
	return count;
}
static int GFX_needsFallback(TextRun* runs, int count, TTF_Font* primary) {
	for (int i=0; i<count; i++) if (runs[i].font!=primary) return 1;
	return 0;
}
int GFX_textWidthWith(TTF_Font* primary, TTF_Font* fb, const char* text) {
	int w = 0;
	if (!fb) { TTF_SizeUTF8(primary, text, &w, NULL); return w; }
	TextRun runs[TEXT_MAX_RUNS];
	int count = GFX_textRuns(primary, fb, text, runs);
	if (!GFX_needsFallback(runs, count, primary)) { TTF_SizeUTF8(primary, text, &w, NULL); return w; }
	for (int i=0; i<count; i++) {
		char piece[512];
		int len = MIN(runs[i].len, (int)sizeof(piece)-1);
		memcpy(piece, text + runs[i].from, len);
		piece[len] = '\0';
		int pw = 0;
		TTF_SizeUTF8(runs[i].font, piece, &pw, NULL);
		w += pw;
	}
	return w;
}
int GFX_textWidth(TTF_Font* font_, const char* text) { return GFX_textWidthWith(font_, GFX_fallbackFont(font_), text); }
SDL_Surface* GFX_renderTextWith(TTF_Font* primary, TTF_Font* fb, const char* text, SDL_Color color) {
	if (!fb || !text || !text[0]) return TTF_RenderUTF8_Blended(primary, text, color);
	TextRun runs[TEXT_MAX_RUNS];
	int count = GFX_textRuns(primary, fb, text, runs);
	if (!GFX_needsFallback(runs, count, primary)) return TTF_RenderUTF8_Blended(primary, text, color);
	SDL_Surface* pieces[TEXT_MAX_RUNS];
	int total_w = 0, max_ascent = 0, height = 0;
	for (int i=0; i<count; i++) {
		char piece[512];
		int len = MIN(runs[i].len, (int)sizeof(piece)-1);
		memcpy(piece, text + runs[i].from, len);
		piece[len] = '\0';
		pieces[i] = TTF_RenderUTF8_Blended(runs[i].font, piece, color);
		if (pieces[i]) {
			total_w += pieces[i]->w;
			max_ascent = MAX(max_ascent, TTF_FontAscent(runs[i].font));
		}
	}
	for (int i=0; i<count; i++) {
		if (!pieces[i]) continue;
		height = MAX(height, (max_ascent - TTF_FontAscent(runs[i].font)) + pieces[i]->h);
	}
	SDL_Surface* out = (total_w>0 && height>0) ? SDL_CreateRGBSurface(SDL_SWSURFACE, total_w, height, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000) : NULL;
	if (out) {
		SDL_FillRect(out, NULL, 0); // transparent
		int x = 0;
		for (int i=0; i<count; i++) {
			SDL_Surface* s = pieces[i];
			if (!s) continue;
			int y0 = max_ascent - TTF_FontAscent(runs[i].font);
			if (s->format->BitsPerPixel==32) {
				for (int y=0; y<s->h && y0+y<out->h; y++) {
					uint32_t* src = (uint32_t*)((uint8_t*)s->pixels + y*s->pitch);
					uint32_t* dst = (uint32_t*)((uint8_t*)out->pixels + (y0+y)*out->pitch);
					for (int xx=0; xx<s->w; xx++) if (src[xx]>>24) dst[x+xx] = src[xx]; // the runs don't overlap: a copy
				}
			}
			x += s->w;
		}
	}
	for (int i=0; i<count; i++) if (pieces[i]) SDL_FreeSurface(pieces[i]);
	return out ? out : TTF_RenderUTF8_Blended(primary, text, color);
}
SDL_Surface* GFX_renderText(TTF_Font* font_, const char* text, SDL_Color color) { return GFX_renderTextWith(font_, GFX_fallbackFont(font_), text, color); }

static void GFX_utf8Trim(char* s) { // drops a multi-byte character that a byte limit cut in half at the end
	size_t len = strlen(s);
	if (len==0) return;
	size_t j = len - 1;
	while (j>0 && ((unsigned char)s[j]&0xC0)==0x80) j--; // back to the lead byte
	unsigned char lead = (unsigned char)s[j];
	if (lead<0xC0) return; // ASCII (or a stray byte): nothing to complete
	size_t need = lead>=0xF0 ? 4 : lead>=0xE0 ? 3 : 2;
	if (len - j < need) s[j] = '\0';
}
int GFX_truncateText(TTF_Font* font, const char* in_name, char* out_name, int max_width, int padding) {
	int text_width;
	snprintf(out_name, 256, "%s", in_name); // NOTE: out_name needs to be at least 256 bytes
	GFX_utf8Trim(out_name);
	text_width = GFX_textWidth(font, out_name);
	text_width += padding;
	
	while (text_width>max_width) {
		int len = strlen(out_name);
		if (len<=3) break; // nothing left to trim but the ellipsis itself
		
		int cut = len-4;
		while (cut>0 && (out_name[cut] & 0xC0)==0x80) cut -= 1; // don't split a multi-byte character
		strcpy(&out_name[cut], "...");
		
		text_width = GFX_textWidth(font, out_name);
		text_width += padding;
	}
	
	return text_width;
}
int GFX_truncateUniqueText(TTF_Font* font, const char* name, const char* unique, char* out_name, char* out_unique, int max_width, int padding) {
	// unique is usually name followed by something that tells it apart from other
	// entries with the same name, eg. " (GBC)", so truncate the name to leave room
	// for that and put it back after, otherwise the tag is the first thing to go
	size_t name_len = strlen(name);
	const char* alt = unique + name_len;
	if (strncmp(unique, name, name_len)!=0 || !alt[0]) { // unrelated (eg. an alias) or nothing to protect
		GFX_truncateText(font, name, out_name, max_width, padding);
		return GFX_truncateText(font, unique, out_unique, max_width, padding);
	}
	
	// what tells it apart is the tag, without the file extension (" (World).gb" is " (World)") unless
	// that leaves nothing, and never more than about half the room, so the name keeps the rest
	char tag[256];
	snprintf(tag, sizeof(tag), "%s", alt);
	GFX_utf8Trim(tag);
	char* dot = strrchr(tag, '.');
	if (dot && dot>tag && dot[1] && strlen(dot+1)<=4) {
		int alnum = 1;
		for (char* q = dot+1; *q; q++) if (!((*q>='0' && *q<='9') || (*q>='a' && *q<='z') || (*q>='A' && *q<='Z'))) alnum = 0;
		if (alnum) *dot = '\0';
	}
	if (!tag[0]) snprintf(tag, sizeof(tag), "%s", alt);
	int alt_width;
	alt_width = GFX_textWidth(font, tag);
	if (alt_width > max_width * 55 / 100) {
		char cut[256];
		GFX_truncateText(font, tag, cut, max_width * 55 / 100, 0);
		snprintf(tag, sizeof(tag), "%s", cut);
		alt_width = GFX_textWidth(font, tag);
	}
	GFX_truncateText(font, name, out_name, max_width-alt_width, padding);
	snprintf(out_unique, 256, "%s%s", out_name, tag);
	GFX_utf8Trim(out_unique);
	
	int text_width;
	text_width = GFX_textWidth(font, out_unique);
	return text_width + padding;
}
int GFX_wrapText(TTF_Font* font, char* str, int max_width, int max_lines) {
	if (!str) return 0;
	
	int line_width;
	int max_line_width = 0;
	char* line = str;
	char buffer[MAX_PATH];
	
	line_width = GFX_textWidth(font, line);
	if (line_width<=max_width) {
		line_width = GFX_truncateText(font,line,buffer,max_width,0);
		strcpy(line,buffer);
		return line_width;
	}
	
	char* prev = NULL;
	char* tmp = line;
	int lines = 1;
	while (!max_lines || lines<max_lines) {
		tmp = strchr(tmp, ' ');
		if (!tmp) {
			if (prev) {
				line_width = GFX_textWidth(font, line);
				if (line_width>=max_width) {
					if (line_width>max_line_width) max_line_width = line_width;
					prev[0] = '\n';
					line = prev + 1;
				}
			}
			break;
		}
		tmp[0] = '\0';
		
		line_width = GFX_textWidth(font, line);

		if (line_width>=max_width && prev) { // wrap (a first word that is too wide has no earlier space to wrap at)
			if (line_width>max_line_width) max_line_width = line_width;
			tmp[0] = ' ';
			tmp += 1;
			prev[0] = '\n';
			prev += 1;
			line = prev;
			lines += 1;
		}
		else { // continue
			tmp[0] = ' ';
			prev = tmp;
			tmp += 1;
		}
	}
	
	line_width = GFX_truncateText(font,line,buffer,max_width,0);
	strcpy(line,buffer);
	
	if (line_width>max_line_width) max_line_width = line_width;
	return max_line_width;
}

///////////////////////////////

// scale_blend (and supporting logic) from picoarch

struct blend_args {
	int w_ratio_in;
	int w_ratio_out;
	uint16_t w_bp[2];
	int h_ratio_in;
	int h_ratio_out;
	uint16_t h_bp[2];
	uint16_t *blend_line;
} blend_args;

#if __ARM_ARCH >= 5
static inline uint32_t average16(uint32_t c1, uint32_t c2) {
	uint32_t ret, lowbits = 0x0821;
	asm ("eor %0, %2, %3\r\n"
	     "and %0, %0, %1\r\n"
	     "add %0, %3, %0\r\n"
	     "add %0, %0, %2\r\n"
	     "lsr %0, %0, #1\r\n"
	     : "=&r" (ret) : "r" (lowbits), "r" (c1), "r" (c2) : );
	return ret;
}
static inline uint32_t average32(uint32_t c1, uint32_t c2) {
	uint32_t ret, lowbits = 0x08210821;

	asm ("eor %0, %3, %1\r\n"
	     "and %0, %0, %2\r\n"
	     "adds %0, %1, %0\r\n"
	     "and %1, %1, #0\r\n"
	     "movcs %1, #0x80000000\r\n"
	     "adds %0, %0, %3\r\n"
	     "rrx %0, %0\r\n"
	     "orr %0, %0, %1\r\n"
	     : "=&r" (ret), "+r" (c2) : "r" (lowbits), "r" (c1) : "cc" );

	return ret;
}

#define AVERAGE16_NOCHK(c1, c2) (average16((c1), (c2)))
#define AVERAGE32_NOCHK(c1, c2) (average32((c1), (c2)))

#else

static inline uint32_t average16(uint32_t c1, uint32_t c2) {
	return (c1 + c2 + ((c1 ^ c2) & 0x0821))>>1;
}
static inline uint32_t average32(uint32_t c1, uint32_t c2) {
	uint32_t sum = c1 + c2;
	uint32_t ret = sum + ((c1 ^ c2) & 0x08210821);
	uint32_t of = ((sum < c1) | (ret < sum)) << 31;

	return (ret >> 1) | of;
}

#define AVERAGE16_NOCHK(c1, c2) (average16((c1), (c2)))
#define AVERAGE32_NOCHK(c1, c2) (average32((c1), (c2)))

#endif

#define AVERAGE16(c1, c2) ((c1) == (c2) ? (c1) : AVERAGE16_NOCHK((c1), (c2)))
#define AVERAGE16_1_3(c1, c2) ((c1) == (c2) ? (c1) : (AVERAGE16_NOCHK(AVERAGE16_NOCHK((c1), (c2)), (c2))))

#define AVERAGE32(c1, c2) ((c1) == (c2) ? (c1) : AVERAGE32_NOCHK((c1), (c2)))
#define AVERAGE32_1_3(c1, c2) ((c1) == (c2) ? (c1) : (AVERAGE32_NOCHK(AVERAGE32_NOCHK((c1), (c2)), (c2))))

static inline int gcd(int a, int b) {
	return b ? gcd(b, a % b) : a;
}

static void scaleAA(void* __restrict src, void* __restrict dst, uint32_t w, uint32_t h, uint32_t pitch, uint32_t dst_w, uint32_t dst_h, uint32_t dst_p) {
	int dy = 0;
	int lines = h;

	int rat_w = blend_args.w_ratio_in;
	int rat_dst_w = blend_args.w_ratio_out;
	uint16_t *bw = blend_args.w_bp;

	int rat_h = blend_args.h_ratio_in;
	int rat_dst_h = blend_args.h_ratio_out;
	uint16_t *bh = blend_args.h_bp;

	while (lines--) {
		while (dy < rat_dst_h) {
			uint16_t *dst16 = (uint16_t *)dst;
			uint16_t *pblend = (uint16_t *)blend_args.blend_line;
			int col = w;
			int dx = 0;

			uint16_t *pnext = (uint16_t *)(src + pitch);
			if (!lines)
				pnext -= (pitch / sizeof(uint16_t));

			if (dy > rat_dst_h - bh[0]) {
				pblend = pnext;
			} else if (dy <= bh[0]) {
				/* Drops const, won't get touched later though */
				pblend = (uint16_t *)src;
			} else {
				const uint32_t *src32 = (const uint32_t *)src;
				const uint32_t *pnext32 = (const uint32_t *)pnext;
				uint32_t *pblend32 = (uint32_t *)pblend;
				int count = w / 2;

				if (dy <= bh[1]) {
					const uint32_t *tmp = pnext32;
					pnext32 = src32;
					src32 = tmp;
				}

				if (dy > rat_dst_h - bh[1] || dy <= bh[1]) {
					while(count--) {
						*pblend32++ = AVERAGE32_1_3(*src32, *pnext32);
						src32++;
						pnext32++;
					}
				} else {
					while(count--) {
						*pblend32++ = AVERAGE32(*src32, *pnext32);
						src32++;
						pnext32++;
					}
				}
			}

			while (col--) {
				uint16_t a, b, out;

				a = *pblend;
				b = *(pblend+1);

				while (dx < rat_dst_w) {
					if (a == b) {
						out = a;
					} else if (dx > rat_dst_w - bw[0]) { // top quintile, bbbb
						out = b;
					} else if (dx <= bw[0]) { // last quintile, aaaa
						out = a;
					} else {
						if (dx > rat_dst_w - bw[1]) { // 2nd quintile, abbb
							a = AVERAGE16_NOCHK(a,b);
						} else if (dx <= bw[1]) { // 4th quintile, aaab
							b = AVERAGE16_NOCHK(a,b);
						}

						out = AVERAGE16_NOCHK(a, b); // also 3rd quintile, aabb
					}
					*dst16++ = out;
					dx += rat_w;
				}

				dx -= rat_dst_w;
				pblend++;
			}

			dy += rat_h;
			dst += dst_p;
		}

		dy -= rat_dst_h;
		src += pitch;
	}
}

scaler_t GFX_getAAScaler(GFX_Renderer* renderer) {
	int gcd_w, div_w, gcd_h, div_h;
	blend_args.blend_line = calloc(renderer->src_w, sizeof(uint16_t));

	gcd_w = gcd(renderer->src_w, renderer->dst_w);
	blend_args.w_ratio_in = renderer->src_w / gcd_w;
	blend_args.w_ratio_out = renderer->dst_w / gcd_w;
	
	double blend_denominator = (renderer->src_w>renderer->dst_w) ? 5 : 2.5; // TODO: these values are really only good for the nano...
	// blend_denominator = 5.0; // better for trimui
	// LOG_info("blend_denominator: %f (%i && %i)\n", blend_denominator, HAS_SKINNY_SCREEN, renderer->dst_w>renderer->src_w);
	
	div_w = round(blend_args.w_ratio_out / blend_denominator);
	blend_args.w_bp[0] = div_w;
	blend_args.w_bp[1] = blend_args.w_ratio_out >> 1;

	gcd_h = gcd(renderer->src_h, renderer->dst_h);
	blend_args.h_ratio_in = renderer->src_h / gcd_h;
	blend_args.h_ratio_out = renderer->dst_h / gcd_h;

	div_h = round(blend_args.h_ratio_out / blend_denominator);
	blend_args.h_bp[0] = div_h;
	blend_args.h_bp[1] = blend_args.h_ratio_out >> 1;
	
	return scaleAA;
}
void GFX_freeAAScaler(void) {
	if (blend_args.blend_line != NULL) {
		free(blend_args.blend_line);
		blend_args.blend_line = NULL;
	}
}

///////////////////////////////

void GFX_blitAsset(int asset, SDL_Rect* src_rect, SDL_Surface* dst, SDL_Rect* dst_rect) {
	SDL_Rect* rect = &asset_rects[asset];
	SDL_Rect adj_rect = {
		.x = rect->x,
		.y = rect->y,
		.w = rect->w,
		.h = rect->h,
	};
	if (src_rect) {
		adj_rect.x += src_rect->x;
		adj_rect.y += src_rect->y;
		adj_rect.w  = src_rect->w;
		adj_rect.h  = src_rect->h;
	}
	SDL_BlitSurface(gfx.assets, &adj_rect, dst, dst_rect);
}
void GFX_blitPill(int asset, SDL_Surface* dst, SDL_Rect* dst_rect) {
	int x = dst_rect->x;
	int y = dst_rect->y;
	int w = dst_rect->w;
	int h = dst_rect->h;

	if (h==0) h = asset_rects[asset].h;
	
	int r = h / 2;
	if (w < h) w = h;
	w -= h;
	
	GFX_blitAsset(asset, &(SDL_Rect){0,0,r,h}, dst, &(SDL_Rect){x,y});
	x += r;
	if (w>0) {
		SDL_FillRect(dst, &(SDL_Rect){x,y,w,h}, asset_rgbs[asset]);
		x += w;
	}
	GFX_blitAsset(asset, &(SDL_Rect){r,0,r,h}, dst, &(SDL_Rect){x,y});
}
void GFX_blitRect(int asset, SDL_Surface* dst, SDL_Rect* dst_rect) {
	int x = dst_rect->x;
	int y = dst_rect->y;
	int w = dst_rect->w;
	int h = dst_rect->h;
	int c = asset_rgbs[asset];
	
	SDL_Rect* rect = &asset_rects[asset];
	int d = rect->w;
	int r = d / 2;

	GFX_blitAsset(asset, &(SDL_Rect){0,0,r,r}, dst, &(SDL_Rect){x,y});
	SDL_FillRect(dst, &(SDL_Rect){x+r,y,w-d,r}, c);
	GFX_blitAsset(asset, &(SDL_Rect){r,0,r,r}, dst, &(SDL_Rect){x+w-r,y});
	SDL_FillRect(dst, &(SDL_Rect){x,y+r,w,h-d}, c);
	GFX_blitAsset(asset, &(SDL_Rect){0,r,r,r}, dst, &(SDL_Rect){x,y+h-r});
	SDL_FillRect(dst, &(SDL_Rect){x+r,y+h-r,w-d,r}, c);
	GFX_blitAsset(asset, &(SDL_Rect){r,r,r,r}, dst, &(SDL_Rect){x+w-r,y+h-r});
}
void GFX_blitBattery(SDL_Surface* dst, SDL_Rect* dst_rect) {
	// LOG_info("dst: %p\n", dst);
	int x = 0;
	int y = 0;
	if (dst_rect) {
		x = dst_rect->x;
		y = dst_rect->y;
	}
	SDL_Rect rect = asset_rects[ASSET_BATTERY];
	x += (SCALE1(PILL_SIZE) - (rect.w + FIXED_SCALE)) / 2;
	y += (SCALE1(PILL_SIZE) - rect.h) / 2;
	
	if (pwr.is_charging) {
		GFX_blitAsset(ASSET_BATTERY, NULL, dst, &(SDL_Rect){x,y});
		GFX_blitAsset(ASSET_BATTERY_BOLT, NULL, dst, &(SDL_Rect){x+SCALE1(3),y+SCALE1(2)});
	}
	else {
		int percent = pwr.charge;
		GFX_blitAsset(percent<=10?ASSET_BATTERY_LOW:ASSET_BATTERY, NULL, dst, &(SDL_Rect){x,y});
		
		rect = asset_rects[ASSET_BATTERY_FILL];
		SDL_Rect clip = rect;
		clip.w *= percent;
		clip.w /= 100;
		if (clip.w<=0) return;
		clip.x = rect.w - clip.w;
		clip.y = 0;
		
		GFX_blitAsset(percent<=20?ASSET_BATTERY_FILL_LOW:ASSET_BATTERY_FILL, &clip, dst, &(SDL_Rect){x+SCALE1(3)+clip.x,y+SCALE1(2)});
	}
}
int GFX_getButtonWidth(char* hint, char* button) {
	int button_width = 0;
	int width;
	
	int special_case = !strcmp(button,BRIGHTNESS_BUTTON_LABEL); // TODO: oof
	
	if (strlen(button)==1) {
		button_width += SCALE1(BUTTON_SIZE);
	}
	else {
		button_width += SCALE1(BUTTON_SIZE) / 2;
		TTF_SizeUTF8(special_case ? font.large : font.tiny, button, &width, NULL);
		button_width += width;
	}
	button_width += SCALE1(BUTTON_MARGIN);
	
	TTF_SizeUTF8(font.small, hint, &width, NULL);
	button_width += width + SCALE1(BUTTON_MARGIN);
	return button_width;
}
void GFX_blitButton(char* hint, char*button, SDL_Surface* dst, SDL_Rect* dst_rect) {
	SDL_Surface* text;
	int ox = 0;
	
	int special_case = !strcmp(button,BRIGHTNESS_BUTTON_LABEL); // TODO: oof
	
	// button
	if (strlen(button)==1) {
		GFX_blitAsset(ASSET_BUTTON, NULL, dst, dst_rect);

		// label
		text = TTF_RenderUTF8_Blended(font.medium, button, COLOR_BUTTON_TEXT);
		SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){dst_rect->x+(SCALE1(BUTTON_SIZE)-text->w)/2,dst_rect->y+(SCALE1(BUTTON_SIZE)-text->h)/2});
		ox += SCALE1(BUTTON_SIZE);
		SDL_FreeSurface(text);
	}
	else {
		text = TTF_RenderUTF8_Blended(special_case ? font.large : font.tiny, button, COLOR_BUTTON_TEXT);
		GFX_blitPill(ASSET_BUTTON, dst, &(SDL_Rect){dst_rect->x,dst_rect->y,SCALE1(BUTTON_SIZE)/2+text->w,SCALE1(BUTTON_SIZE)});
		ox += SCALE1(BUTTON_SIZE)/4;
		
		int oy = special_case ? SCALE1(-2) : 0;
		SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){ox+dst_rect->x,oy+dst_rect->y+(SCALE1(BUTTON_SIZE)-text->h)/2,text->w,text->h});
		ox += text->w;
		ox += SCALE1(BUTTON_SIZE)/4;
		SDL_FreeSurface(text);
	}
	
	ox += SCALE1(BUTTON_MARGIN);

	// hint text
	text = TTF_RenderUTF8_Blended(font.small, hint, COLOR_WHITE);
	SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){ox+dst_rect->x,dst_rect->y+(SCALE1(BUTTON_SIZE)-text->h)/2,text->w,text->h});
	SDL_FreeSurface(text);
}
void GFX_blitMessage(TTF_Font* font, char* msg, SDL_Surface* dst, SDL_Rect* dst_rect) {
	if (!dst_rect) dst_rect = &(SDL_Rect){0,0,dst->w,dst->h};
	
	// LOG_info("GFX_blitMessage: %p (%ix%i)", dst, dst_rect->w,dst_rect->h);
	
	SDL_Surface* text;
#define TEXT_BOX_MAX_ROWS 16
#define LINE_HEIGHT 24
	char* rows[TEXT_BOX_MAX_ROWS];
	int row_count = 0;

	char* tmp;
	rows[row_count++] = msg;
	while ((tmp=strchr(rows[row_count-1], '\n'))!=NULL) {
		if (row_count>=TEXT_BOX_MAX_ROWS) break; // too many rows, draw the ones that fit
		rows[row_count++] = tmp+1;
	}
	
	int rendered_height = SCALE1(LINE_HEIGHT) * row_count;
	int y = dst_rect->y;
	y += (dst_rect->h - rendered_height) / 2;
	
	char line[256];
	for (int i=0; i<row_count; i++) {
		// a row ends at the next newline (or the end of the message)
		// and is cut to fit, cores provide some of these strings
		char* end = strchr(rows[i], '\n');
		int len = end ? end-rows[i] : strlen(rows[i]);
		if (len>=sizeof(line)) len = sizeof(line)-1;
		if (len) memcpy(line, rows[i], len);
		line[len] = '\0';
		GFX_utf8Trim(line);
		len = strlen(line);
		
		if (len) {
			text = GFX_renderText(font, line, COLOR_WHITE);
			if (text) {
				int x = dst_rect->x;
				x += (dst_rect->w - text->w) / 2;
				SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){x,y});
				SDL_FreeSurface(text);
			}
		}
		y += SCALE1(LINE_HEIGHT);
	}
}

static char hud_toast_text[48] = "";
// The slot picker under the save and load previews: the numbers 1 to count, the current one in a dark
// pill, slots that hold a save in black and empty ones in gray, and < > at the ends to say that left
// and right change the slot. Drawn in the area (x,y,w) one row high (SCALE1(SLOT_PICKER_HEIGHT)).
void GFX_fillCapsule(SDL_Surface* dst, SDL_Rect r, Uint32 color) { // a filled rounded block, ends as round as the height allows
	int radius = r.h / 2;
	if (r.w < r.h) radius = r.w / 2;
	for (int row=0; row<r.h; row++) {
		int d = row < radius ? radius - row : (row >= r.h - radius ? row - (r.h - radius - 1) : 0); // rows from the straight part
		int inset = radius - (int)(sqrt((double)radius*radius - (double)d*d) + 0.5);
		if (d==0) inset = 0;
		SDL_FillRect(dst, &(SDL_Rect){r.x + inset, r.y + row, r.w - inset*2, 1}, color);
	}
}
void GFX_blitSlotPicker(SDL_Surface* dst, int x, int y, int w, int slot, int count, const int* used) {
	int cw = SCALE1(17); // width of one number
	int rh = SCALE1(SLOT_PICKER_HEIGHT);
	int row_w = cw * count;
	int rx = x + (w - row_w) / 2;
	
	const char* arrows[2] = { "<", ">" };
	for (int a=0; a<2; a++) {
		SDL_Surface* t = TTF_RenderUTF8_Blended(font.large, arrows[a], COLOR_DARK_TEXT);
		if (!t) continue;
		int ax = a==0 ? rx - SCALE1(2) - t->w : rx + row_w + SCALE1(2);
		SDL_BlitSurface(t, NULL, dst, &(SDL_Rect){ax, y + (rh - t->h) / 2});
		SDL_FreeSurface(t);
	}
	for (int i=0; i<count; i++) {
		char number[8];
		snprintf(number, sizeof(number), "%i", i+1);
		int cx = rx + i * cw;
		SDL_Color color = (used && used[i]) ? COLOR_BLACK : COLOR_GRAY;
		if (i==slot) {
			GFX_fillCapsule(dst, (SDL_Rect){cx + SCALE1(1), y, cw - SCALE1(2), rh}, SDL_MapRGB(dst->format, 0, 0, 0));
			color = COLOR_WHITE;
		}
		SDL_Surface* t = TTF_RenderUTF8_Blended(font.large, number, color);
		if (!t) continue;
		SDL_BlitSurface(t, NULL, dst, &(SDL_Rect){cx + (cw - t->w) / 2, y + (rh - t->h) / 2});
		SDL_FreeSurface(t);
	}
}

// SSH icon (see api.h): is something listening on a TCP port, from /proc/net/tcp (or tcp6)
int Net_listening(const char* text, int port) {
	for (const char* p = text; p && *p; ) {
		unsigned local_port = 0, state = 0;
		if (sscanf(p, " %*d: %*[0-9A-Fa-f]:%x %*[0-9A-Fa-f]:%*x %x", &local_port, &state)==2 && state==0x0A && (int)local_port==port) return 1; // 0A: LISTEN
		p = strchr(p, '\n');
		if (p) p++;
	}
	return 0;
}
// end of the SSH icon code
// 1 while SSH is on (Tools > SSH starts the service until the next reboot); looked at every few seconds, not every frame
static int sshOn(void) {
	static uint32_t checked_at = 0;
	static int on = 0;
	uint32_t now = SDL_GetTicks();
	if (checked_at && now - checked_at < 3000) return on;
	checked_at = now ? now : 1;
	if (getenv("MINUI_FAKE_SSH")) return on = 1; // for the Mac checks
	on = 0;
	static const char* files[] = { "/proc/net/tcp", "/proc/net/tcp6" };
	for (int i=0; i<2 && !on; i++) {
		FILE* f = fopen(files[i], "r"); // a line at a time: a file in /proc says its size is 0 (allocFile would see nothing), and a long table must not be cut short
		if (!f) continue;
		char line[256];
		while (!on && fgets(line, sizeof(line), f)) on = Net_listening(line, 22);
		fclose(f);
	}
	return on;
}
// the clock next to the Wi-Fi and battery icons: 0 off, 12 or 24 hour. Read once per run (the Settings tool sets it).
static int clockMode(void) {
	static int mode = -1;
	if (mode<0) {
		char text[16] = "";
		getFile(SHOW_CLOCK_PATH, text, sizeof(text));
		mode = atoi(text);
		if (mode!=12 && mode!=24) mode = 0;
	}
	return mode;
}
static void clockText(char* out, size_t size) {
	time_t t = time(NULL);
	struct tm tmv;
	out[0] = '\0';
	if (!localtime_r(&t, &tmv)) return;
	if (clockMode()==12) {
		int h = tmv.tm_hour % 12;
		snprintf(out, size, "%i:%02i %s", h ? h : 12, tmv.tm_min, tmv.tm_hour<12 ? "AM" : "PM");
	}
	else snprintf(out, size, "%02i:%02i", tmv.tm_hour, tmv.tm_min);
}
int GFX_clockChanged(void) { // 1 when the clock is on and its minute has changed since the last call
	static int last = -1;
	if (!clockMode()) return 0;
	time_t t = time(NULL);
	struct tm tmv;
	if (!localtime_r(&t, &tmv) || tmv.tm_min==last) return 0;
	last = tmv.tm_min;
	return 1;
}
int GFX_blitHardwareGroup(SDL_Surface* dst, int show_setting) {
	int ox;
	int oy;
	int ow = 0;
	
	int setting_value;
	int setting_min;
	int setting_max;
	
	if (show_setting==HUD_TOAST && !GetHDMI()) { // a short message in the same pill
		SDL_Surface* text = TTF_RenderUTF8_Blended(font.medium, hud_toast_text, COLOR_WHITE);
		if (text) {
			ow = text->w + SCALE1(PILL_SIZE);
			ox = dst->w - SCALE1(PADDING) - ow;
			oy = SCALE1(PADDING);
			GFX_blitPill(ASSET_BLACK_PILL, dst, &(SDL_Rect){ox, oy, ow, SCALE1(PILL_SIZE)});
			SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){ox + (ow-text->w)/2, oy + (SCALE1(PILL_SIZE)-text->h)/2});
			SDL_FreeSurface(text);
		}
	}
	else if (show_setting && !GetHDMI()) {
		ow = SCALE1(PILL_SIZE + SETTINGS_WIDTH + 10 + 4);
		ox = dst->w - SCALE1(PADDING) - ow;
		oy = SCALE1(PADDING);
		GFX_blitPill(gfx.mode==MODE_MAIN ? ASSET_DARK_GRAY_PILL : ASSET_BLACK_PILL, dst, &(SDL_Rect){
			ox,
			oy,
			ow,
			SCALE1(PILL_SIZE)
		});
		
		if (show_setting==1) {
			setting_value = GetBrightness();
			setting_min = BRIGHTNESS_MIN;
			setting_max = BRIGHTNESS_MAX;
		}
		else {
			setting_value = GetVolume();
			setting_min = VOLUME_MIN;
			setting_max = VOLUME_MAX;
		}
		
		int asset = show_setting==1?ASSET_BRIGHTNESS:(setting_value>0?ASSET_VOLUME:ASSET_VOLUME_MUTE);
		int ax = ox + (show_setting==1 ? SCALE1(6) : SCALE1(8));
		int ay = oy + (show_setting==1 ? SCALE1(5) : SCALE1(7));
		GFX_blitAsset(asset, NULL, dst, &(SDL_Rect){ax,ay});
		
		ox += SCALE1(PILL_SIZE);
		oy += SCALE1((PILL_SIZE - SETTINGS_SIZE) / 2);
		GFX_blitPill(gfx.mode==MODE_MAIN ? ASSET_BAR_BG : ASSET_BAR_BG_MENU, dst, &(SDL_Rect){
			ox,
			oy,
			SCALE1(SETTINGS_WIDTH),
			SCALE1(SETTINGS_SIZE)
		});
		
		float percent = ((float)(setting_value-setting_min) / (setting_max-setting_min));
		if (show_setting==1 || setting_value>0) {
			GFX_blitPill(ASSET_BAR, dst, &(SDL_Rect){
				ox,
				oy,
				SCALE1(SETTINGS_WIDTH) * percent,
				SCALE1(SETTINGS_SIZE)
			});
		}
	}
	else {
		// TODO: handle wifi
		int show_wifi = PLAT_isOnline(); // NOOOOO! not every frame!

		int ww = SCALE1(PILL_SIZE-3);
		ow = SCALE1(PILL_SIZE);
		if (show_wifi) ow += ww;
		char clock_text[24];
		SDL_Surface* clock_surface = NULL;
		if (clockMode()) {
			clockText(clock_text, sizeof(clock_text));
			clock_surface = TTF_RenderUTF8_Blended(font.medium, clock_text, COLOR_WHITE);
			if (clock_surface) ow += clock_surface->w + SCALE1(12);
		}

		ox = dst->w - SCALE1(PADDING) - ow;
		oy = SCALE1(PADDING);
		GFX_blitPill(gfx.mode==MODE_MAIN ? ASSET_DARK_GRAY_PILL : ASSET_BLACK_PILL, dst, &(SDL_Rect){
			ox,
			oy,
			ow,
			SCALE1(PILL_SIZE)
		});
		if (clock_surface) {
			SDL_BlitSurface(clock_surface, NULL, dst, &(SDL_Rect){ox + SCALE1(10), oy + (SCALE1(PILL_SIZE) - clock_surface->h)/2});
			ox += clock_surface->w + SCALE1(12);
			SDL_FreeSurface(clock_surface);
		}
		if (show_wifi) {
			SDL_Rect rect = asset_rects[ASSET_WIFI];
			int x = ox;
			int y = oy;
			x += (SCALE1(PILL_SIZE) - rect.w) / 2;
			y += (SCALE1(PILL_SIZE) - rect.h) / 2;
			
			GFX_blitAsset(ASSET_WIFI, NULL, dst, &(SDL_Rect){x,y});
			if (sshOn()) { // a green dot on the icon: SSH is on
				int d = SCALE1(5);
				GFX_fillCapsule(dst, (SDL_Rect){x + rect.w - d + SCALE1(2), y - SCALE1(1), d, d}, SDL_MapRGB(dst->format, 0x4C, 0xD9, 0x64));
			}
			ox += ww;
		}
		GFX_blitBattery(dst, &(SDL_Rect){ox,oy});
	}
	
	return ow;
}
// The volume/brightness pill for in-game feedback. The game is drawn straight from the
// core's buffer at its own size, so the pill cannot be drawn into the screen surface.
// Instead it is drawn at device size, once on white and once on black, and the difference
// between the two gives each pixel's alpha (the blits and fills of the menu have no alpha
// of their own). The platform composites the result over the game (PLAT_setHud).
SDL_Surface* GFX_renderSettingHud(int show_setting) {
	static SDL_Surface* on_white = NULL;
	static SDL_Surface* on_black = NULL;
	static SDL_Surface* hud = NULL;
	int w = gfx.screen->w;
	int h = SCALE1(PILL_SIZE + PADDING * 2);
	if (!hud || hud->w!=w || hud->h!=h) {
		if (hud) SDL_FreeSurface(hud);
		if (on_white) SDL_FreeSurface(on_white);
		if (on_black) SDL_FreeSurface(on_black);
		on_white = SDL_CreateRGBSurface(SDL_SWSURFACE, w,h, 16, RGBA_MASK_565);
		on_black = SDL_CreateRGBSurface(SDL_SWSURFACE, w,h, 16, RGBA_MASK_565);
		hud = SDL_CreateRGBSurface(SDL_SWSURFACE, w,h, 32, 0x00FF0000,0x0000FF00,0x000000FF,0xFF000000);
		if (!on_white || !on_black || !hud) return NULL;
	}
	SDL_FillRect(on_white, NULL, 0xFFFF);
	SDL_FillRect(on_black, NULL, 0);
	GFX_blitHardwareGroup(on_white, show_setting);
	GFX_blitHardwareGroup(on_black, show_setting);
	
	for (int y=0; y<h; y++) {
		uint16_t* white = (uint16_t*)((uint8_t*)on_white->pixels + y * on_white->pitch);
		uint16_t* black = (uint16_t*)((uint8_t*)on_black->pixels + y * on_black->pitch);
		uint32_t* out = (uint32_t*)((uint8_t*)hud->pixels + y * hud->pitch);
		for (int x=0; x<w; x++) {
			int wc[3] = { (white[x]>>11 & 0x1F) * 255 / 31, (white[x]>>5 & 0x3F) * 255 / 63, (white[x] & 0x1F) * 255 / 31 };
			int bc[3] = { (black[x]>>11 & 0x1F) * 255 / 31, (black[x]>>5 & 0x3F) * 255 / 63, (black[x] & 0x1F) * 255 / 31 };
			int d = 0;
			for (int c=0; c<3; c++) if (wc[c]-bc[c]>d) d = wc[c]-bc[c];
			int a = 255 - d;
			if (a<=0) { out[x] = 0; continue; }
			int rgb[3];
			for (int c=0; c<3; c++) { rgb[c] = bc[c] * 255 / a; if (rgb[c]>255) rgb[c] = 255; }
			out[x] = (uint32_t)a<<24 | (uint32_t)rgb[0]<<16 | (uint32_t)rgb[1]<<8 | (uint32_t)rgb[2];
		}
	}
	return hud;
}
SDL_Surface* GFX_renderToastHud(const char* text) {
	snprintf(hud_toast_text, sizeof(hud_toast_text), "%s", text);
	return GFX_renderSettingHud(HUD_TOAST);
}
// platforms that can draw it override this (see rgb30)
__attribute__((weak)) void PLAT_setHud(SDL_Surface* hud) {}
// the exact charge, 0-100 (PLAT_getBatteryStatus only has steps), -1 if the platform can't tell
__attribute__((weak)) int PLAT_getBatteryPercent(void) { return -1; }

void GFX_blitHardwareHints(SDL_Surface* dst, int show_setting) {
	if (BTN_MOD_VOLUME==BTN_SELECT && BTN_MOD_BRIGHTNESS==BTN_START) {
		if (show_setting==1) GFX_blitButtonGroup((char*[]){ "SELECT","VOLUME",  NULL }, 0, dst, 0);
		else GFX_blitButtonGroup((char*[]){ "START","BRIGHTNESS",  NULL }, 0, dst, 0);
	}
	else {
		if (show_setting==1) GFX_blitButtonGroup((char*[]){ BRIGHTNESS_BUTTON_LABEL,"BRIGHTNESS",  NULL }, 0, dst, 0);
		else GFX_blitButtonGroup((char*[]){ "MENU","BRIGHTNESS",  NULL }, 0, dst, 0);
	}
	
}

int GFX_blitButtonGroup(char** pairs, int primary, SDL_Surface* dst, int align_right) {
	int ox;
	int oy;
	int ow;
	char* hint;
	char* button;

	struct Hint {
		char* hint;
		char* button;
		int ow;
	} hints[2]; 
	int w = 0; // individual button dimension
	int h = 0; // hints index
	ow = 0; // full pill width
	ox = align_right ? dst->w - SCALE1(PADDING) : SCALE1(PADDING);
	oy = dst->h - SCALE1(PADDING + PILL_SIZE);
	
	for (int i=0; i<2; i++) {
		if (!pairs[i*2]) break;
		if (HAS_SKINNY_SCREEN && i!=primary) continue; // space saving
		
		button = pairs[i * 2];
		hint = pairs[i * 2 + 1];
		w = GFX_getButtonWidth(hint, button);
		hints[h].hint = hint;
		hints[h].button = button;
		hints[h].ow = w;
		h += 1;
		ow += SCALE1(BUTTON_MARGIN) + w;
	}
	
	ow += SCALE1(BUTTON_MARGIN);
	if (align_right) ox -= ow;
	GFX_blitPill(gfx.mode==MODE_MAIN ? ASSET_DARK_GRAY_PILL : ASSET_BLACK_PILL, dst, &(SDL_Rect){
		ox,
		oy,
		ow,
		SCALE1(PILL_SIZE)
	});
	
	ox += SCALE1(BUTTON_MARGIN);
	oy += SCALE1(BUTTON_MARGIN);
	for (int i=0; i<h; i++) {
		GFX_blitButton(hints[i].hint, hints[i].button, dst, &(SDL_Rect){ox,oy});
		ox += hints[i].ow + SCALE1(BUTTON_MARGIN);
	}
	return ow;
}

#define MAX_TEXT_LINES 16
void GFX_sizeText(TTF_Font* font, char* str, int leading, int* w, int* h) {
	char* lines[MAX_TEXT_LINES];
	int count = 0;

	char* tmp;
	lines[count++] = str;
	while ((tmp=strchr(lines[count-1], '\n'))!=NULL) {
		if (count+1>MAX_TEXT_LINES) break; // TODO: bail?
		lines[count++] = tmp+1;
	}
	*h = count * leading;
	
	int mw = 0;
	char line[256];
	for (int i=0; i<count; i++) {
		int len;
		if (i+1<count) len = lines[i+1]-lines[i]-1;
		else len = strlen(lines[i]);
		if (len>(int)sizeof(line)-1) len = sizeof(line)-1; // a long line (a cheat code, a core's text) is cut, not run off the buffer
		if (len>0) memcpy(line, lines[i], len);
		else len = 0;
		line[len] = '\0';
		GFX_utf8Trim(line);
		len = strlen(line);
		
		if (len) {
			int lw;
			lw = GFX_textWidth(font, line);
			if (lw>mw) mw = lw;
		}
	}
	*w = mw;
}
void GFX_blitText(TTF_Font* font, char* str, int leading, SDL_Color color, SDL_Surface* dst, SDL_Rect* dst_rect) {
	if (dst_rect==NULL) dst_rect = &(SDL_Rect){0,0,dst->w,dst->h};
	
	char* lines[MAX_TEXT_LINES];
	int count = 0;

	char* tmp;
	lines[count++] = str;
	while ((tmp=strchr(lines[count-1], '\n'))!=NULL) {
		if (count+1>MAX_TEXT_LINES) break; // TODO: bail?
		lines[count++] = tmp+1;
	}
	int x = dst_rect->x;
	int y = dst_rect->y;
	
	SDL_Surface* text;
	char line[256];
	for (int i=0; i<count; i++) {
		int len;
		if (i+1<count) len = lines[i+1]-lines[i]-1;
		else len = strlen(lines[i]);
		if (len>(int)sizeof(line)-1) len = sizeof(line)-1; // a long line (a cheat code, a core's text) is cut, not run off the buffer
		if (len>0) memcpy(line, lines[i], len);
		else len = 0;
		line[len] = '\0';
		GFX_utf8Trim(line);
		len = strlen(line);
		
		if (len) {
			text = GFX_renderText(font, line, color);
			SDL_BlitSurface(text, NULL, dst, &(SDL_Rect){x+((dst_rect->w-text->w)/2),y+(i*leading)});
			SDL_FreeSurface(text);
		}
	}
}

///////////////////////////////

// based on picoarch's audio 
// implementation, rewritten 
// to (try to) understand it 
// better

#define MAX_SAMPLE_RATE 48000
#define BATCH_SIZE 100
#ifndef SAMPLES
	#define SAMPLES 512 // default
#endif

#define ms SDL_GetTicks

typedef int (*SND_Resampler)(const SND_Frame frame);
static struct SND_Context {
	int initialized;
	int disabled;			// the audio device did not open: samples are thrown away (waiting for a buffer that never drains would freeze the game)
	double frame_rate;
	
	int sample_rate_in;
	int sample_rate_out;
	
	int buffer_seconds;     // current_audio_buffer_size
	SND_Frame* buffer;		// buf
	size_t frame_count; 	// buf_len
	
	int frame_in;     // buf_w
	int frame_out;    // buf_r
	int frame_filled; // max_buf_w
	
	SND_Resampler resample;
	int drc_rate_in; // sample_rate_in nudged by how full the buffer is, see SND_updateRate()
	int drc_enabled; // off if USERDATA_PATH has a "no-audio-rate-control" file
	FILE* dump;			// audio-dump file in USERDATA_PATH: the core's raw audio goes to /tmp/audio-dump-<rate>.raw (diagnostic)
	size_t dump_bytes;
	int target_ms; // 0: the default cushion (a third of the buffer); else the audio-buffer file's milliseconds
	
	// more diagnostics, logged every few seconds and on exit
	int peak_in;			// loudest sample written to the buffer since the last log line
	int peak_out;			// loudest sample the callback took out of it (written by the audio thread)
	int drc_rate_min;
	int drc_rate_max;
	int fill_max;
	unsigned frames_offered;	// frames the core has handed us
	unsigned frames_written;	// frames put in the buffer (a bit more or less after rate control)
	unsigned frames_duplicated;	// frames written twice by the rate control
	unsigned frames_dropped;	// frames left out by the rate control
	int fade_pos;				// frames of the fade-in done so far
	int fade_len;				// length of the fade-in in frames, 0 for none
	unsigned log_at;			// frames_offered at the last log line
	
	// diagnostics, only written by the audio callback and read after audio is closed
	unsigned callbacks;			// times the callback ran
	unsigned starved;			// callbacks that found the buffer completely empty
	unsigned underruns;			// callbacks that ran out of frames part way through
	unsigned underrun_frames;	// frames those callbacks were short by
} snd = {0};
static void SND_audioFill(void* userdata, uint8_t* stream, int len) {
	
	// return (void)memset(stream,0,len); // TODO: tmp, silent
	
	if (snd.frame_count==0) return;
	
	int16_t *out = (int16_t *)stream;
	len /= (sizeof(int16_t) * 2);
	// int full_len = len;
	
	// if (snd.frame_out!=snd.frame_in) LOG_info("%8i consuming samples (%i frames)\n", ms(), len);
	
	while (snd.frame_out!=snd.frame_in && len>0) {
		*out++ = snd.buffer[snd.frame_out].left;
		*out++ = snd.buffer[snd.frame_out].right;
		{ int p = abs(snd.buffer[snd.frame_out].left); int q = abs(snd.buffer[snd.frame_out].right); if (q>p) p = q; if (p>snd.peak_out) snd.peak_out = p; }
		
		snd.frame_filled = snd.frame_out;
		
		snd.frame_out += 1;
		len -= 1;
		
		if (snd.frame_out>=snd.frame_count) snd.frame_out = 0;
	}
	
	snd.callbacks++;
	
	int zero = len>0 && len==SAMPLES;
	if (zero) {
		snd.starved++;
		return (void)memset(out,0,len*(sizeof(int16_t) * 2));
	}
	// else if (len>=5) LOG_info("%8i BUFFER UNDERRUN (%i/%i frames)\n", ms(), len,full_len);
	
	if (len>0) {
		snd.underruns++;
		snd.underrun_frames += len;
	}
	
	// ran out of frames, mirror the ones just written (a whole frame at a time,
	// left with left and right with right) so the waveform stays continuous
	// instead of dropping to silence, and fall back to silence once they run out
	int16_t* frames = (int16_t*)stream;
	int src = (out-frames) / 2 - 1; // last frame written
	while (len>0) {
		if (src>=0) {
			*out++ = frames[src*2];
			*out++ = frames[src*2+1];
			src -= 1;
		}
		else {
			*out++ = 0;
			*out++ = 0;
		}
		len -= 1;
	}
}
// Ramp the first moments of a game's sound up from silence. Cores often start at a
// non-zero level (the NES one does), and a jump from the silence before it to that
// level is a click from the speaker. Counting starts at the first non-silent frame.
static void SND_fadeIn(int16_t* out, int frames) {
	if (snd.fade_pos>=snd.fade_len) return;
	for (int i=0; i<frames && snd.fade_pos<snd.fade_len; i++) {
		if (snd.fade_pos==0 && !out[i*2] && !out[i*2+1]) continue;
		out[i*2]   = (int16_t)((int)out[i*2]   * snd.fade_pos / snd.fade_len);
		out[i*2+1] = (int16_t)((int)out[i*2+1] * snd.fade_pos / snd.fade_len);
		snd.fade_pos += 1;
	}
}
static void SND_audioCallback(void* userdata, uint8_t* stream, int len) { // plat_sound_callback
	SND_audioFill(userdata, stream, len);
	SND_fadeIn((int16_t*)stream, len / (int)(sizeof(int16_t) * 2));
}
#define DRC_TARGET_DIVISOR 3 // aim for a third of the buffer (~1330 frames, 28ms at 48kHz): with a quarter
	// a late frame still ran the buffer dry now and then, and the rate swung back and forth more
static void SND_resizeBuffer(void) { // plat_sound_resize_buffer
	snd.frame_count = snd.buffer_seconds * snd.sample_rate_in / snd.frame_rate;
	if (snd.target_ms>0) { // a bigger cushion needs a bigger buffer: the rate control aims for a third of it
		size_t want = (size_t)DRC_TARGET_DIVISOR * snd.target_ms * snd.sample_rate_in / 1000;
		if (want>snd.frame_count) snd.frame_count = want;
	}
	if (snd.frame_count==0) return;
	
	// LOG_info("frame_count: %i (%i * %i / %f)\n", snd.frame_count, snd.buffer_seconds, snd.sample_rate_in, snd.frame_rate);
	// snd.frame_count *= 2; // no help
	
	SDL_LockAudio();
	
	int buffer_bytes = snd.frame_count * sizeof(SND_Frame);
	snd.buffer = realloc(snd.buffer, buffer_bytes);
	
	memset(snd.buffer, 0, buffer_bytes);
	
	snd.frame_in = snd.target_ms>0 ? snd.frame_count / DRC_TARGET_DIVISOR : 0; // start with the cushion already there (silence, under the fade-in)
	snd.frame_out = 0;
	snd.frame_filled = snd.frame_count - 1;
	snd.drc_rate_in = snd.sample_rate_in;
	
	SDL_UnlockAudio();
}
static int SND_resampleNear(SND_Frame frame) { // audio_resample_nearest
	static int diff = 0;
	int consumed = 0;
	int wrote = 0;

	if (diff < snd.sample_rate_out) {
		{ int p = abs(frame.left); int q = abs(frame.right); if (q>p) p = q; if (p>snd.peak_in) snd.peak_in = p; }
		snd.frames_written++;
		wrote = 1;
		snd.buffer[snd.frame_in++] = frame;
		if (snd.frame_in >= snd.frame_count) snd.frame_in = 0;
		diff += snd.drc_rate_in;
	}

	if (diff >= snd.sample_rate_out) {
		consumed++;
		diff -= snd.sample_rate_out;
	}
	if (wrote && !consumed) snd.frames_duplicated++;
	else if (!wrote && consumed) snd.frames_dropped++;

	return consumed;
}
static void SND_selectResampler(void) { // plat_sound_select_resampler
	// always, even when the rates match: with drc_rate_in equal to sample_rate_out this
	// is a straight copy, and SND_updateRate() needs it to be able to stretch the audio
	snd.resample = SND_resampleNear;
}
// Dynamic rate control. Frames are paced by the video (a fixed budget plus vsync, about
// 60fps) but a core produces audio for its own frame rate (NES 60.0988, so about 0.16%
// more than 60 paced frames would play), and the device consumes it at a fixed 48kHz.
// Whenever the two disagree the buffer slowly fills or drains, and when it drains the
// callback runs dry every few calls: crackling. So nudge the rate the core's audio is
// resampled at by how full the buffer is (at most MAX_DELTA either way, 1% is
// inaudible) which keeps the buffer around its target instead.
#define DRC_MAX_DELTA 0.01
static void SND_updateRate(void) {
	if (!snd.drc_enabled) { snd.drc_rate_in = snd.sample_rate_in; return; }
	
	int fill = snd.frame_in - snd.frame_out;
	if (fill<0) fill += snd.frame_count;
	int target = snd.frame_count / DRC_TARGET_DIVISOR;
	if (target<1) target = 1;
	
	double error = (double)(target - fill) / target; // positive when low, we need more audio
	if (error>1) error = 1;
	else if (error<-1) error = -1;
	
	// the less input per output frame, the more output frames we make
	snd.drc_rate_in = snd.sample_rate_in * (1.0 - error * DRC_MAX_DELTA);
	if (snd.drc_rate_in<snd.drc_rate_min) snd.drc_rate_min = snd.drc_rate_in;
	if (snd.drc_rate_in>snd.drc_rate_max) snd.drc_rate_max = snd.drc_rate_in;
	if (fill>snd.fill_max) snd.fill_max = fill;
}
size_t SND_batchSamples(const SND_Frame* frames, size_t frame_count) { // plat_sound_write / plat_sound_write_resample
	
	// return frame_count; // TODO: tmp, silent
	
	if (snd.frame_count==0) return 0;
	if (snd.disabled) return frame_count;
	
	// LOG_info("%8i batching samples (%i frames)\n", ms(), frame_count);
	
	SDL_LockAudio();
	snd.frames_offered += frame_count;
	if (snd.dump && snd.dump_bytes < 40u*1024*1024) { // about 5 minutes, then it stops
		snd.dump_bytes += fwrite(frames, sizeof(SND_Frame), frame_count, snd.dump) * sizeof(SND_Frame);
	}

	int consumed = 0;
	int consumed_frames = 0;
	while (frame_count > 0) {
		int tries = 0;
		SND_updateRate();
		int amount = MIN(BATCH_SIZE, frame_count);
		
		while (tries < 10 && snd.frame_in==snd.frame_filled) {
			tries++;
			SDL_UnlockAudio();
			SDL_Delay(1);
			SDL_LockAudio();
		}
		// if (tries) LOG_info("%8i waited %ims for buffer to get low...\n", ms(), tries);

		while (amount && snd.frame_in != snd.frame_filled) {
			consumed_frames = snd.resample(*frames);
			
			frames += consumed_frames;
			amount -= consumed_frames;
			frame_count -= consumed_frames;
			consumed += consumed_frames;
		}
	}
	if (snd.frames_offered - snd.log_at >= 5u * snd.sample_rate_in) {
		snd.log_at = snd.frames_offered;
		int fill = snd.frame_in - snd.frame_out; if (fill<0) fill += snd.frame_count;
		LOG_info("audio: fill %i (max %i) rate %i..%i of %i, loudest in %i out %i, callbacks %u starved %u underruns %u\n", fill, snd.fill_max, snd.drc_rate_min, snd.drc_rate_max, snd.sample_rate_in, snd.peak_in, snd.peak_out, snd.callbacks, snd.starved, snd.underruns);
		snd.peak_in = snd.peak_out = 0;
	}
	SDL_UnlockAudio();
	
	return consumed;
}

// Where the sound is routed, for the next time the device is silent although the buffers
// are healthy. The mixer SDL/ALSA reaches by default is PipeWire's, not the codec's, so the
// codec (card 0) and PipeWire are both read. The ALSA stream state is read at once (cheap),
// the PipeWire queries run in the background so they don't hold up the game.
static void SND_logState(const char* when) {
	fflush(stdout);
	char cmd[1024];
	snprintf(cmd, sizeof(cmd),
		"echo '-- audio state (%s)'; "
		"amixer -c 0 cget name='Playback Path' 2>&1 | tail -4; "
		"amixer -c 0 scontents 2>&1 | head -24; "
		"for f in /proc/asound/card*/pcm*p/sub0/status; do echo $f; cat $f 2>&1; done; "
		"( echo '-- pipewire (%s)'; pactl info 2>&1 | grep -i 'default sink\\|server name'; "
		"pactl list short sinks 2>&1; pactl get-sink-mute @DEFAULT_SINK@ 2>&1; pactl get-sink-volume @DEFAULT_SINK@ 2>&1; "
		"systemctl is-active pipewire pipewire-pulse wireplumber 2>&1 | tr '\\n' ' '; echo ) &",
		when, when);
	system(cmd);
	fflush(stdout);
}

// .userdata/<platform>/audio-buffer: how many milliseconds of sound to keep ready (20 to 500).
// More rides out a busy moment in a game instead of dropping a sound, at the cost of that much delay.
// Nothing, 0 or anything unreadable keeps the default (about 28ms).
static int SND_parseBufferMs(const char* text) {
	if (!text || *text<'0' || *text>'9') return 0;
	int n = 0;
	while (*text>='0' && *text<='9' && n<10000) n = n*10 + (*text++ - '0');
	while (*text==' ' || *text=='\t' || *text=='\r' || *text=='\n') text++;
	if (*text) return 0;
	return (n>=20 && n<=500) ? n : 0;
}
void SND_init(double sample_rate, double frame_rate) { // plat_sound_init
	LOG_info("SND_init\n");
	
	SDL_InitSubSystem(SDL_INIT_AUDIO);
	
#if defined(USE_SDL2)
	LOG_info("Available audio drivers:\n");
	for (int i=0; i<SDL_GetNumAudioDrivers(); i++) {
		LOG_info("- %s\n", SDL_GetAudioDriver(i));
	}
	LOG_info("Current audio driver: %s\n", SDL_GetCurrentAudioDriver());
#endif	
	
	memset(&snd, 0, sizeof(struct SND_Context));
	snd.frame_rate = frame_rate;

	SDL_AudioSpec spec_in;
	SDL_AudioSpec spec_out;
	memset(&spec_in, 0, sizeof(spec_in));
	memset(&spec_out, 0, sizeof(spec_out));

	spec_in.freq = PLAT_pickSampleRate(sample_rate, MAX_SAMPLE_RATE);
	spec_in.format = AUDIO_S16;
	spec_in.channels = 2;
	spec_in.samples = SAMPLES;
	spec_in.callback = SND_audioCallback;
	
	if (SDL_OpenAudio(&spec_in, &spec_out)<0) {
		LOG_info("SDL_OpenAudio error: %s\n", SDL_GetError());
		spec_out = spec_in; // nothing was opened: the rates still have to be something
		snd.disabled = 1;
	}
	
	snd.buffer_seconds = 5;
	{
		char text[16] = "";
		FILE* f = fopen(USERDATA_PATH "/audio-buffer", "r");
		if (f) { if (!fgets(text, sizeof(text), f)) text[0] = '\0'; fclose(f); }
		snd.target_ms = SND_parseBufferMs(text);
	}
	snd.sample_rate_in  = sample_rate;
	snd.sample_rate_out = spec_out.freq;
	if (exists(USERDATA_PATH "/audio-dump")) { // diagnostic: what the core produced, before any of our processing
		char path[64];
		snprintf(path, sizeof(path), "/tmp/audio-dump-%i.raw", (int)sample_rate);
		snd.dump = fopen(path, "wb");
		LOG_info("audio dump: %s (%s)\n", path, snd.dump ? "recording" : "can't open");
	}
	
	snd.fade_pos = 0;
	snd.fade_len = snd.sample_rate_out / 20; // 50ms
	snd.drc_enabled = !exists(USERDATA_PATH "/no-audio-rate-control");
	snd.drc_rate_min = snd.drc_rate_max = snd.sample_rate_in;
	SND_selectResampler();
	SND_resizeBuffer();
	
	SDL_PauseAudio(0);

	LOG_info("sample rate: %i (req) %i (rec) [samples %i]\n", snd.sample_rate_in, snd.sample_rate_out, SAMPLES);
	LOG_info("audio rate control: %s\n", snd.drc_enabled ? "on" : "off (no-audio-rate-control)");
	LOG_info("audio buffer: %i frames, cushion %s\n", (int)snd.frame_count, snd.target_ms ? "set by audio-buffer" : "default");
	
	SND_logState("start");
	snd.initialized = 1;
}
void SND_quit(void) { // plat_sound_finish
	if (!snd.initialized) return;
	
	SND_logState("quit"); // stream still open
	SDL_PauseAudio(1);
	SDL_CloseAudio();
	
	LOG_info("audio: %u callbacks, %u starved (buffer empty), %u underruns (%u frames short)\n", snd.callbacks, snd.starved, snd.underruns, snd.underrun_frames);
	LOG_info("audio: %u frames from the core, %u written (rate control %+i: %u frames repeated, %u left out)\n", snd.frames_offered, snd.frames_written, (int)snd.frames_written - (int)snd.frames_offered, snd.frames_duplicated, snd.frames_dropped);
	
	if (snd.dump) {
		fclose(snd.dump);
		snd.dump = NULL;
	}
	if (snd.buffer) {
		free(snd.buffer);
		snd.buffer = NULL;
	}
}

///////////////////////////////

LID_Context lid = {
	.has_lid = 0,
	.is_open = 1,
};

FALLBACK_IMPLEMENTATION void PLAT_initLid(void) {  }
FALLBACK_IMPLEMENTATION int PLAT_lidChanged(int* state) { return 0; }

///////////////////////////////

PAD_Context pad;

// how far the left stick must move (percent of full travel) to count as a D-pad press;
// .userdata/<platform>/stick-deadzone holds a number from 10 to 90
#define STICK_DEADZONE_DEFAULT 40
static int PAD_parseDeadzone(const char* text) {
	if (!text || *text<'0' || *text>'9') return STICK_DEADZONE_DEFAULT;
	int n = 0;
	while (*text>='0' && *text<='9' && n<1000) n = n*10 + (*text++ - '0');
	if (*text=='%') text++;
	while (*text==' ' || *text=='\t' || *text=='\r' || *text=='\n') text++;
	if (*text) return STICK_DEADZONE_DEFAULT; // trailing junk
	if (n<10 || n>90) return STICK_DEADZONE_DEFAULT;
	return n;
}
static int PAD_deadzone(void) { // axis value (of 32767), read once
	static int zone = -1;
	if (zone<0) {
		char text[16] = "";
		FILE* f = fopen(USERDATA_PATH "/stick-deadzone", "r");
		int percent = STICK_DEADZONE_DEFAULT;
		if (f) {
			if (fgets(text, sizeof(text), f)) percent = PAD_parseDeadzone(text);
			fclose(f);
		}
		zone = 32767 * percent / 100;
		LOG_info("stick deadzone %i%%\n", percent);
	}
	return zone;
}
void PAD_setAnalog(int neg_id,int pos_id,int value,int repeat_at) {
	// LOG_info("neg %i pos %i value %i\n", neg_id, pos_id, value);
	int neg = 1 << neg_id;
	int pos = 1 << pos_id;	
	int deadzone = PAD_deadzone();
	if (value>deadzone) { // pressing
		if (!(pad.is_pressed&pos)) { // not pressing
			pad.is_pressed 		|= pos; // set
			pad.just_pressed	|= pos; // set
			pad.just_repeated	|= pos; // set
			pad.repeat_at[pos_id]= repeat_at;
		
			if (pad.is_pressed&neg) { // was pressing opposite
				pad.is_pressed 		&= ~neg; // unset
				pad.just_repeated 	&= ~neg; // unset
				pad.just_released	|=  neg; // set
			}
		}
	}
	else if (value<-deadzone) { // pressing
		if (!(pad.is_pressed&neg)) { // not pressing
			pad.is_pressed		|= neg; // set
			pad.just_pressed	|= neg; // set
			pad.just_repeated	|= neg; // set
			pad.repeat_at[neg_id]= repeat_at;
		
			if (pad.is_pressed&pos) { // was pressing opposite
				pad.is_pressed 		&= ~pos; // unset
				pad.just_repeated 	&= ~pos; // unset
				pad.just_released	|=  pos; // set
			}
		}
	}
	else { // not pressing
		if (pad.is_pressed&neg) { // was pressing
			pad.is_pressed 		&= ~neg; // unset
			pad.just_repeated	&= ~neg; // unset
			pad.just_released	|=  neg; // set
		}
		if (pad.is_pressed&pos) { // was pressing
			pad.is_pressed 		&= ~pos; // unset
			pad.just_repeated	&= ~pos; // unset
			pad.just_released	|=  pos; // set
		}
	}
}

void PAD_reset(void) {
	// LOG_info("PAD_reset");
	pad.just_pressed = BTN_NONE;
	pad.is_pressed = BTN_NONE;
	pad.just_released = BTN_NONE;
	pad.just_repeated = BTN_NONE;
}
FALLBACK_IMPLEMENTATION void PLAT_pollInput(void) {
	// reset transient state
	pad.just_pressed = BTN_NONE;
	pad.just_released = BTN_NONE;
	pad.just_repeated = BTN_NONE;

	uint32_t tick = SDL_GetTicks();
	for (int i=0; i<BTN_ID_COUNT; i++) {
		int btn = 1 << i;
		if ((pad.is_pressed & btn) && (tick>=pad.repeat_at[i])) {
			pad.just_repeated |= btn; // set
			pad.repeat_at[i] += PAD_REPEAT_INTERVAL;
		}
	}
	
	// the actual poll
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		int btn = BTN_NONE;
		int pressed = 0; // 0=up,1=down
		int id = -1;
		if (event.type==SDL_KEYDOWN || event.type==SDL_KEYUP) {
			uint8_t code = event.key.keysym.scancode;
			pressed = event.type==SDL_KEYDOWN;
			// LOG_info("key event: %i (%i)\n", code,pressed);
				 if (code==CODE_UP) 		{ btn = BTN_DPAD_UP; 		id = BTN_ID_DPAD_UP; }
 			else if (code==CODE_DOWN)		{ btn = BTN_DPAD_DOWN; 		id = BTN_ID_DPAD_DOWN; }
			else if (code==CODE_LEFT)		{ btn = BTN_DPAD_LEFT; 		id = BTN_ID_DPAD_LEFT; }
			else if (code==CODE_RIGHT)		{ btn = BTN_DPAD_RIGHT; 	id = BTN_ID_DPAD_RIGHT; }
			else if (code==CODE_A)			{ btn = BTN_A; 				id = BTN_ID_A; }
			else if (code==CODE_B)			{ btn = BTN_B; 				id = BTN_ID_B; }
			else if (code==CODE_X)			{ btn = BTN_X; 				id = BTN_ID_X; }
			else if (code==CODE_Y)			{ btn = BTN_Y; 				id = BTN_ID_Y; }
			else if (code==CODE_START)		{ btn = BTN_START; 			id = BTN_ID_START; }
			else if (code==CODE_SELECT)		{ btn = BTN_SELECT; 		id = BTN_ID_SELECT; }
			else if (code==CODE_MENU)		{ btn = BTN_MENU; 			id = BTN_ID_MENU; }
			else if (code==CODE_MENU_ALT)	{ btn = BTN_MENU; 			id = BTN_ID_MENU; }
			else if (code==CODE_L1)			{ btn = BTN_L1; 			id = BTN_ID_L1; }
			else if (code==CODE_L2)			{ btn = BTN_L2; 			id = BTN_ID_L2; }
			else if (code==CODE_L3)			{ btn = BTN_L3; 			id = BTN_ID_L3; }
			else if (code==CODE_R1)			{ btn = BTN_R1; 			id = BTN_ID_R1; }
			else if (code==CODE_R2)			{ btn = BTN_R2; 			id = BTN_ID_R2; }
			else if (code==CODE_R3)			{ btn = BTN_R3; 			id = BTN_ID_R3; }
			else if (code==CODE_PLUS)		{ btn = BTN_PLUS; 			id = BTN_ID_PLUS; }
			else if (code==CODE_MINUS)		{ btn = BTN_MINUS; 			id = BTN_ID_MINUS; }
			else if (code==CODE_POWER)		{ btn = BTN_POWER; 			id = BTN_ID_POWER; }
			else if (code==CODE_POWEROFF)	{ btn = BTN_POWEROFF;		id = BTN_ID_POWEROFF; } // nano-only
		}
		else if (event.type==SDL_JOYBUTTONDOWN || event.type==SDL_JOYBUTTONUP) {
			uint8_t joy = event.jbutton.button;
			pressed = event.type==SDL_JOYBUTTONDOWN;
			// LOG_info("joy event: %i (%i)\n", joy,pressed);
				 if (joy==JOY_UP) 		{ btn = BTN_DPAD_UP; 		id = BTN_ID_DPAD_UP; }
 			else if (joy==JOY_DOWN)		{ btn = BTN_DPAD_DOWN; 		id = BTN_ID_DPAD_DOWN; }
			else if (joy==JOY_LEFT)		{ btn = BTN_DPAD_LEFT; 		id = BTN_ID_DPAD_LEFT; }
			else if (joy==JOY_RIGHT)	{ btn = BTN_DPAD_RIGHT; 	id = BTN_ID_DPAD_RIGHT; }
			else if (joy==JOY_A)		{ btn = BTN_A; 				id = BTN_ID_A; }
			else if (joy==JOY_B)		{ btn = BTN_B; 				id = BTN_ID_B; }
			else if (joy==JOY_X)		{ btn = BTN_X; 				id = BTN_ID_X; }
			else if (joy==JOY_Y)		{ btn = BTN_Y; 				id = BTN_ID_Y; }
			else if (joy==JOY_START)	{ btn = BTN_START; 			id = BTN_ID_START; }
			else if (joy==JOY_SELECT)	{ btn = BTN_SELECT; 		id = BTN_ID_SELECT; }
			else if (joy==JOY_MENU)		{ btn = BTN_MENU; 			id = BTN_ID_MENU; }
			else if (joy==JOY_MENU_ALT) { btn = BTN_MENU; 			id = BTN_ID_MENU; }
			else if (joy==JOY_MENU_ALT2){ btn = BTN_MENU; 			id = BTN_ID_MENU; }
			else if (joy==JOY_L1)		{ btn = BTN_L1; 			id = BTN_ID_L1; }
			else if (joy==JOY_L2)		{ btn = BTN_L2; 			id = BTN_ID_L2; }
			else if (joy==JOY_L3)		{ btn = BTN_L3; 			id = BTN_ID_L3; }
			else if (joy==JOY_R1)		{ btn = BTN_R1; 			id = BTN_ID_R1; }
			else if (joy==JOY_R2)		{ btn = BTN_R2; 			id = BTN_ID_R2; }
			else if (joy==JOY_R3)		{ btn = BTN_R3; 			id = BTN_ID_R3; }
			else if (joy==JOY_PLUS)		{ btn = BTN_PLUS; 			id = BTN_ID_PLUS; }
			else if (joy==JOY_MINUS)	{ btn = BTN_MINUS; 			id = BTN_ID_MINUS; }
			else if (joy==JOY_POWER)	{ btn = BTN_POWER; 			id = BTN_ID_POWER; }
		}
		else if (event.type==SDL_JOYHATMOTION) {
			int hats[4] = {-1,-1,-1,-1}; // -1=no change,0=up,1=down,2=left,3=right btn_ids
			int hat = event.jhat.value;
			// LOG_info("hat event: %i\n", hat);
			// TODO: safe to assume hats will always be the primary dpad?
			// TODO: this is literally a bitmask, make it one (oh, except there's 3 states...)
			switch (hat) {
				case SDL_HAT_UP:			hats[0]=1;	  hats[1]=0;	hats[2]=0;	  hats[3]=0;	break;
				case SDL_HAT_DOWN:			hats[0]=0;	  hats[1]=1;	hats[2]=0;	  hats[3]=0;	break;
				case SDL_HAT_LEFT:			hats[0]=0;	  hats[1]=0;	hats[2]=1;	  hats[3]=0;	break;
				case SDL_HAT_RIGHT:			hats[0]=0;	  hats[1]=0;	hats[2]=0;	  hats[3]=1;	break;
				case SDL_HAT_LEFTUP:		hats[0]=1;	  hats[1]=0;	hats[2]=1;	  hats[3]=0;	break;
				case SDL_HAT_LEFTDOWN:		hats[0]=0;	  hats[1]=1;	hats[2]=1;	  hats[3]=0;	break;
				case SDL_HAT_RIGHTUP:		hats[0]=1;	  hats[1]=0;	hats[2]=0;	  hats[3]=1;	break;
				case SDL_HAT_RIGHTDOWN:		hats[0]=0;	  hats[1]=1;	hats[2]=0;	  hats[3]=1;	break;
				case SDL_HAT_CENTERED:		hats[0]=0;	  hats[1]=0;	hats[2]=0;	  hats[3]=0;	break;
				default: break;
			}
			
			for (id=0; id<4; id++) {
				int state = hats[id];
				btn = 1 << id;
				if (state==0) {
					pad.is_pressed		&= ~btn; // unset
					pad.just_repeated	&= ~btn; // unset
					pad.just_released	|= btn; // set
				}
				else if (state==1 && (pad.is_pressed & btn)==BTN_NONE) {
					pad.just_pressed	|= btn; // set
					pad.just_repeated	|= btn; // set
					pad.is_pressed		|= btn; // set
					pad.repeat_at[id]	= tick + PAD_REPEAT_DELAY;
				}
			}
			btn = BTN_NONE; // already handled, force continue
		}
		else if (event.type==SDL_JOYAXISMOTION) {
			int axis = event.jaxis.axis;
			int val = event.jaxis.value;
			// LOG_info("axis: %i (%i)\n", axis,val);
			
			// triggers on tg5040
			if (axis==AXIS_L2) {
				btn = BTN_L2;
				id = BTN_ID_L2;
				pressed = val>0;
			}
			else if (axis==AXIS_R2) {
				btn = BTN_R2;
				id = BTN_ID_R2;
				pressed = val>0;
			}
			
			else if (axis==AXIS_LX) { pad.laxis.x = val; PAD_setAnalog(BTN_ID_ANALOG_LEFT, BTN_ID_ANALOG_RIGHT, val, tick+PAD_REPEAT_DELAY); }
			else if (axis==AXIS_LY) { pad.laxis.y = val; PAD_setAnalog(BTN_ID_ANALOG_UP,   BTN_ID_ANALOG_DOWN,  val, tick+PAD_REPEAT_DELAY); }
			else if (axis==AXIS_RX) pad.raxis.x = val;
			else if (axis==AXIS_RY) pad.raxis.y = val;
			
			// axis will fire off what looks like a release
			// before the first press but you can't release
			// a button that wasn't pressed
			if (!pressed && btn!=BTN_NONE && !(pad.is_pressed & btn)) {
				// LOG_info("cancel: %i\n", axis);
				btn = BTN_NONE;
			}
		}
		// else if (event.type==SDL_QUIT) PWR_powerOff(); // added for macOS debug
		
		if (btn==BTN_NONE) continue;
		
		if (!pressed) {
			pad.is_pressed		&= ~btn; // unset
			pad.just_repeated	&= ~btn; // unset
			pad.just_released	|= btn; // set
		}
		else if ((pad.is_pressed & btn)==BTN_NONE) {
			pad.just_pressed	|= btn; // set
			pad.just_repeated	|= btn; // set
			pad.is_pressed		|= btn; // set
			pad.repeat_at[id]	= tick + PAD_REPEAT_DELAY;
		}
	}
	
	if (lid.has_lid && PLAT_lidChanged(NULL)) pad.just_released |= BTN_SLEEP;
}
FALLBACK_IMPLEMENTATION int PLAT_shouldWake(void) {
	int lid_open = 1; // assume open by default
	if (lid.has_lid && PLAT_lidChanged(&lid_open) && lid_open) return 1;
	
	
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type==SDL_KEYUP) {
			uint8_t code = event.key.keysym.scancode;
			if ((BTN_WAKE==BTN_POWER && code==CODE_POWER) || (BTN_WAKE==BTN_MENU && (code==CODE_MENU || code==CODE_MENU_ALT))) {
				// ignore input while lid is closed
				if (lid.has_lid && !lid.is_open) return 0;  // do it here so we eat the input
				return 1;
			}
		}
		else if (event.type==SDL_JOYBUTTONUP) {
			uint8_t joy = event.jbutton.button;
			if ((BTN_WAKE==BTN_POWER && joy==JOY_POWER) || (BTN_WAKE==BTN_MENU && (joy==JOY_MENU || joy==JOY_MENU_ALT))) {
				// ignore input while lid is closed
				if (lid.has_lid && !lid.is_open) return 0;  // do it here so we eat the input
				return 1;
			}
		} 
	}
	return 0;
}

int PAD_anyJustPressed(void)	{ return pad.just_pressed!=BTN_NONE; }
int PAD_anyPressed(void)		{ return pad.is_pressed!=BTN_NONE; }
int PAD_anyJustReleased(void)	{ return pad.just_released!=BTN_NONE; }

int PAD_justPressed(int btn)	{ return pad.just_pressed & btn; }
int PAD_isPressed(int btn)		{ return pad.is_pressed & btn; }
int PAD_justReleased(int btn)	{ return pad.just_released & btn; }
int PAD_justRepeated(int btn)	{ return pad.just_repeated & btn; }

int PAD_tappedMenu(uint32_t now) {
	#define MENU_DELAY 250 // also in PWR_update()
	static uint32_t menu_start = 0;
	static int ignore_menu = 0; 
	if (PAD_justPressed(BTN_MENU)) {
		ignore_menu = 0;
		menu_start = now;
	}
	else if (PAD_isPressed(BTN_MENU) && BTN_MOD_BRIGHTNESS==BTN_MENU && (PAD_justPressed(BTN_MOD_PLUS) || PAD_justPressed(BTN_MOD_MINUS))) {
		ignore_menu = 1;
	}
	return (!ignore_menu && PAD_justReleased(BTN_MENU) && now-menu_start<MENU_DELAY);
}

///////////////////////////////

// How strong the motor runs, as a percent of what the game asks for: .userdata/<platform>/rumble, 0 turns it
// off, 100 (also when missing or unreadable) is as the game asks.
static int VIB_parsePercent(const char* text) {
	if (!text || *text<'0' || *text>'9') return 100;
	int n = 0;
	while (*text>='0' && *text<='9' && n<1000) n = n*10 + (*text++ - '0');
	if (*text=='%') text++;
	while (*text==' ' || *text=='\t' || *text=='\r' || *text=='\n') text++;
	if (*text || n>100) return 100;
	return n;
}
static int VIB_scale(int strength) {
	static int percent = -1;
	if (percent<0) {
		char text[16] = "";
		FILE* f = fopen(USERDATA_PATH "/rumble", "r");
		if (f) { if (!fgets(text, sizeof(text), f)) text[0] = '\0'; fclose(f); percent = VIB_parsePercent(text); }
		else percent = 100;
		LOG_info("rumble at %i%%\n", percent);
	}
	return (int)((long long)strength * percent / 100);
}
static struct VIB_Context {
	int initialized;
	pthread_t pt;
	int queued_strength;
	int strength;
} vib = {0};
static void* VIB_thread(void *arg) {
#define DEFER_FRAMES 3
	static int defer = 0;
	while(1) {
		SDL_Delay(17);
		if (vib.queued_strength!=vib.strength) {
			if (defer<DEFER_FRAMES && vib.queued_strength==0) { // minimize vacillation between 0 and some number (which this motor doesn't like)
				defer += 1;
				continue;
			}
			vib.strength = vib.queued_strength;
			defer = 0;

			PLAT_setRumble(VIB_scale(vib.strength));
		}
	}
	return 0;
}
void VIB_init(void) {
	vib.queued_strength = vib.strength = 0;
	pthread_create(&vib.pt, NULL, &VIB_thread, NULL);
	vib.initialized = 1;
}
void VIB_quit(void) {
	if (!vib.initialized) return;
	
	VIB_setStrength(0);
	pthread_cancel(vib.pt);
	pthread_join(vib.pt, NULL);
	PLAT_setRumble(0); // the thread may not have got to the 0, and a motor left running keeps running
}
void VIB_setStrength(int strength) {
	if (vib.queued_strength==strength) return;
	vib.queued_strength = strength;
}
int VIB_getStrength(void) {
	return vib.strength;
}

///////////////////////////////

static void PWR_initOverlay(void) {
	// setup surface
	pwr.overlay = PLAT_initOverlay();

	// draw battery
	SDLX_SetAlpha(gfx.assets, 0,0);
	GFX_blitAsset(ASSET_BLACK_PILL, NULL, pwr.overlay, NULL);
	SDLX_SetAlpha(gfx.assets, SDL_SRCALPHA,0);
	GFX_blitBattery(pwr.overlay, NULL);
}

static void PWR_updateBatteryStatus(void) {
	PLAT_getBatteryStatus(&pwr.is_charging, &pwr.charge);
	PLAT_enableOverlay(pwr.should_warn && pwr.charge<=PWR_LOW_CHARGE);
}

static void* PWR_monitorBattery(void *arg) {
	while(1) {
		// TODO: the frequency of checking could depend on whether 
		// we're in game (less frequent) or menu (more frequent)
		sleep(5);
		PWR_updateBatteryStatus();
	}
	return NULL;
}

void PWR_init(void) {
	pwr.can_sleep = 1;
	pwr.can_poweroff = 1;
	pwr.can_autosleep = 1;
	
	pwr.requested_sleep = 0;
	pwr.requested_wake = 0;
	
	pwr.should_warn = 0;
	pwr.charge = PWR_LOW_CHARGE;
	
	PWR_initOverlay();

	PWR_updateBatteryStatus();
	pthread_create(&pwr.battery_pt, NULL, &PWR_monitorBattery, NULL);
	pwr.initialized = 1;
}
void PWR_quit(void) {
	if (!pwr.initialized) return;
	
	PLAT_quitOverlay();
	
	// cancel battery thread
	pthread_cancel(pwr.battery_pt);
	pthread_join(pwr.battery_pt, NULL);
}
void PWR_warn(int enable) {
	pwr.should_warn = enable;
	PLAT_enableOverlay(pwr.should_warn && pwr.charge<=PWR_LOW_CHARGE);
}

int PWR_ignoreSettingInput(int btn, int show_setting) {
	return show_setting && (btn==BTN_MOD_PLUS || btn==BTN_MOD_MINUS);
}

// How long with no input before the device goes to sleep by itself. It was a fixed 30 seconds.
// Now 2 minutes unless USERDATA_PATH/sleep-timeout says otherwise: a number of seconds,
// 0 for never (10 to 86400 otherwise, anything else is ignored and the default is used).
#define SLEEP_TIMEOUT_DEFAULT 120
static int PWR_parseSleepSeconds(const char* text) { // seconds, 0 for never
	if (!text) return SLEEP_TIMEOUT_DEFAULT;
	while (*text==' ' || *text=='\t') text++;
	if (*text<'0' || *text>'9') return SLEEP_TIMEOUT_DEFAULT;
	long n = 0;
	for (; *text>='0' && *text<='9'; text++) {
		n = n*10 + (*text-'0');
		if (n>86400) return SLEEP_TIMEOUT_DEFAULT;
	}
	while (*text==' ' || *text=='\t' || *text=='\r' || *text=='\n') text++;
	if (*text) return SLEEP_TIMEOUT_DEFAULT; // trailing junk, eg. "2 minutes"
	if (n!=0 && n<10) return SLEEP_TIMEOUT_DEFAULT; // a typo should not make it sleep at once
	return (int)n;
}
// seconds from a one-line file in .userdata/<platform>/, 0 for never (the Power tool writes these)
static int PWR_readSeconds(const char* path) {
	char text[32] = "";
	FILE* f = fopen(path, "r");
	if (!f) return SLEEP_TIMEOUT_DEFAULT;
	if (!fgets(text, sizeof(text), f)) text[0] = '\0';
	fclose(f);
	return PWR_parseSleepSeconds(text);
}
static uint32_t PWR_sleepDelay(void) { // milliseconds, 0 for never; read once
	static int seconds = -1;
	if (seconds<0) {
		seconds = PWR_readSeconds(USERDATA_PATH "/sleep-timeout");
		LOG_info("autosleep after %i seconds\n", seconds);
	}
	return (uint32_t)seconds * 1000;
}
static uint32_t PWR_poweroffDelay(void) { // how long it stays asleep before powering off, 0 for never; read once
	static int seconds = -1;
	if (seconds<0) {
		seconds = PWR_readSeconds(USERDATA_PATH "/poweroff-timeout");
		LOG_info("power off after %i seconds asleep\n", seconds);
	}
	return (uint32_t)seconds * 1000;
}

// Nearly flat battery: below this percent, and not charging, the game is quicksaved and the device powers
// off, instead of dying in the middle of a write. .userdata/<platform>/low-battery-shutdown, 0 for never.
#define LOW_BATTERY_DEFAULT 5
#define LOW_BATTERY_MAX 20
#define LOW_BATTERY_GRACE_MS 60000 // after starting: time to plug a charger in
static int PWR_parseLowBattery(const char* text) {
	if (!text || *text<'0' || *text>'9') return LOW_BATTERY_DEFAULT;
	int n = 0;
	while (*text>='0' && *text<='9' && n<1000) n = n*10 + (*text++ - '0');
	if (*text=='%') text++;
	while (*text==' ' || *text=='\t' || *text=='\r' || *text=='\n') text++;
	if (*text || n>LOW_BATTERY_MAX) return LOW_BATTERY_DEFAULT;
	return n;
}
static int PWR_lowBatteryLimit(void) { // percent, 0 for never; read once
	static int percent = -1;
	if (percent<0) {
		char text[16] = "";
		FILE* f = fopen(USERDATA_PATH "/low-battery-shutdown", "r");
		if (f) { if (!fgets(text, sizeof(text), f)) text[0] = '\0'; fclose(f); percent = PWR_parseLowBattery(text); }
		else percent = LOW_BATTERY_DEFAULT;
		LOG_info("power off at %i%% battery\n", percent);
	}
	return percent;
}
// Is the battery flat? Only after `needed` readings in a row at or below the limit, never while
// charging or when the charge isn't known (-1), so one odd reading can't shut the device down.
static int PWR_lowBatteryCheck(int percent, int charging, int limit, int* strikes, int needed) {
	if (limit<=0 || percent<0 || charging || percent>limit) { *strikes = 0; return 0; }
	return ++(*strikes) >= needed;
}

void PWR_update(int* _dirty, int* _show_setting, PWR_callback_t before_sleep, PWR_callback_t after_sleep) {
	int dirty = _dirty ? *_dirty : 0;
	int show_setting = _show_setting ? *_show_setting : 0;
	
	static uint32_t last_input_at = 0; // timestamp of last input (autosleep)
	static uint32_t checked_charge_at = 0; // timestamp of last time checking charge
	static uint32_t setting_shown_at = 0; // timestamp when settings started being shown
	static uint32_t power_pressed_at = 0; // timestamp when power button was just pressed
	static uint32_t mod_unpressed_at = 0; // timestamp of last time settings modifier key was NOT down
	static uint32_t was_muted = -1;
	if (was_muted==-1) was_muted = GetMute();
	
	static int was_charging = -1;
	if (was_charging==-1) was_charging = pwr.is_charging;

	uint32_t now = SDL_GetTicks();
	if (was_charging || PAD_anyPressed() || last_input_at==0) last_input_at = now;
	
	#define CHARGE_DELAY 1000
	if (dirty || now-checked_charge_at>=CHARGE_DELAY) {
		int is_charging = pwr.is_charging;
		if (was_charging!=is_charging) {
			was_charging = is_charging;
			dirty = 1;
		}
		checked_charge_at = now;
	}
	
	if (PAD_justReleased(BTN_POWEROFF) || (power_pressed_at && now-power_pressed_at>=1000)) {
		if (before_sleep) before_sleep();
		PWR_powerOff();
	}
	
	static uint32_t battery_checked_at = 0;
	static int low_strikes = 0;
	if (now-battery_checked_at>=5000) {
		battery_checked_at = now;
		int percent = PLAT_getBatteryPercent();
		if (pwr.can_poweroff && now>=LOW_BATTERY_GRACE_MS && PWR_lowBatteryCheck(percent, pwr.is_charging, PWR_lowBatteryLimit(), &low_strikes, 3)) {
			LOG_info("battery at %i%%, saving and powering off\n", percent);
			pwr.low_battery = 1;
			if (before_sleep) before_sleep();
			PWR_powerOff();
		}
	}
	
	if (PAD_justPressed(BTN_POWER)) {
		power_pressed_at = now;
	}
	
	uint32_t sleep_delay = PWR_sleepDelay(); // 0: never
	if (sleep_delay && now-last_input_at>=sleep_delay && PWR_preventAutosleep()) last_input_at = now;
	
	if (
		pwr.requested_sleep || // hardware requested sleep
		(sleep_delay && now-last_input_at>=sleep_delay) || // autosleep
		(pwr.can_sleep && PAD_justReleased(BTN_SLEEP)) // manual sleep
	) {
		pwr.requested_sleep = 0;
		if (before_sleep) before_sleep();
		PWR_fauxSleep();
		if (after_sleep) after_sleep();
		
		last_input_at = now = SDL_GetTicks();
		power_pressed_at = 0;
		dirty = 1;
	}
	
	
	// TODO: only delay hiding setting changes if that setting didn't require a modifier button be held, otherwise release as soon as modifier is released
	
	int delay_settings = BTN_MOD_BRIGHTNESS==BTN_MENU; // when both volume and brighness require a modifier hide settings as soon as it is released
	#define SETTING_DELAY 500
	if (show_setting && (now-setting_shown_at>=SETTING_DELAY || !delay_settings) && !PAD_isPressed(BTN_MOD_VOLUME) && !PAD_isPressed(BTN_MOD_BRIGHTNESS)) {
		show_setting = 0;
		dirty = 1;
	}
	
	if (!show_setting && !PAD_isPressed(BTN_MOD_VOLUME) && !PAD_isPressed(BTN_MOD_BRIGHTNESS)) {
		mod_unpressed_at = now; // this feels backwards but is correct
	}
	
	#define MOD_DELAY 250
	if (
		(
			(PAD_isPressed(BTN_MOD_VOLUME) || PAD_isPressed(BTN_MOD_BRIGHTNESS)) && 
			(!delay_settings || now-mod_unpressed_at>=MOD_DELAY)
		) || 
		((!BTN_MOD_VOLUME || !BTN_MOD_BRIGHTNESS) && (PAD_justRepeated(BTN_MOD_PLUS) || PAD_justRepeated(BTN_MOD_MINUS)))
	) {
		setting_shown_at = now;
		if (PAD_isPressed(BTN_MOD_BRIGHTNESS)) {
			show_setting = 1;
		}
		else {
			show_setting = 2;
		}
	}
	
	int muted = GetMute();
	if (muted!=was_muted) {
		was_muted = muted;
		show_setting = 2;
		setting_shown_at = now;
	}
	
	if (show_setting) dirty = 1; // shm is slow or keymon is catching input on the next frame
	if (_dirty) *_dirty = dirty;
	if (_show_setting) *_show_setting = show_setting;
}

// TODO: this isn't whether it can sleep but more if it should sleep in response to the sleep button
void PWR_disableSleep(void) {
	pwr.can_sleep = 0;
}
void PWR_enableSleep(void) {
	pwr.can_sleep = 1;
}

void PWR_disablePowerOff(void) {
	pwr.can_poweroff = 0;
}
void PWR_powerOff(void) {
	if (pwr.can_poweroff) {
		
		int w = FIXED_WIDTH;
		int h = FIXED_HEIGHT;
		int p = FIXED_PITCH;
		if (GetHDMI()) {
			w = HDMI_WIDTH;
			h = HDMI_HEIGHT;
			p = HDMI_PITCH;
		}
		gfx.screen = GFX_resize(w,h,p);
		
		char* msg;
		if (pwr.low_battery) msg = exists(AUTO_RESUME_PATH) ? "Battery empty,\nquicksave created,\npowering off" : "Battery empty,\npowering off";
		else if (HAS_POWER_BUTTON || HAS_POWEROFF_BUTTON) msg = exists(AUTO_RESUME_PATH) ? "Quicksave created,\npowering off" : "Powering off";
		else msg = exists(AUTO_RESUME_PATH) ? "Quicksave created,\npower off now" : "Power off now";
		
		// LOG_info("PWR_powerOff %s (%ix%i)\n", gfx.screen, gfx.screen->w, gfx.screen->h);
		
		// TODO: for some reason screen's dimensions end up being 0x0 in GFX_blitMessage...
		PLAT_clearVideo(gfx.screen);
		GFX_blitMessage(font.large, msg, gfx.screen,&(SDL_Rect){0,0,gfx.screen->w,gfx.screen->h}); //, NULL);
		GFX_flip(gfx.screen);
		PLAT_powerOff();
	}
}

static void PWR_enterSleep(void) {
	SDL_PauseAudio(1);
	if (GetHDMI()) {
		PLAT_clearVideo(gfx.screen);
		PLAT_flip(gfx.screen, 0);
	}
	else {
		SetRawVolume(MUTE_VOLUME_RAW);
		PLAT_enableBacklight(0);
	}
	system("killall -STOP keymon.elf");
	
	sync();
}
static void PWR_exitSleep(void) {
	system("killall -CONT keymon.elf");
	if (GetHDMI()) {
		// buh
	}
	else {
		PLAT_enableBacklight(1);
		SetVolume(GetVolume());
	}
	SDL_PauseAudio(0);
	
	sync();
}

static void PWR_waitForWake(void) {
	uint32_t sleep_ticks = SDL_GetTicks();
	uint32_t off_delay = PWR_poweroffDelay(); // 0: stay asleep until woken
	uint32_t battery_checked_at = sleep_ticks;
	int low_strikes = 0;
	while (!PAD_wake()) {
		if (pwr.requested_wake) {
			pwr.requested_wake = 0;
			break;
		}
		SDL_Delay(200);
		if (SDL_GetTicks()-battery_checked_at>=30000) { // asleep still drains the battery: the game was saved going to sleep
			battery_checked_at = SDL_GetTicks();
			if (PWR_lowBatteryCheck(PLAT_getBatteryPercent(), pwr.is_charging, PWR_lowBatteryLimit(), &low_strikes, 2)) {
				pwr.low_battery = 1;
				PWR_powerOff();
			}
		}
		if (off_delay && pwr.can_poweroff && SDL_GetTicks()-sleep_ticks>=off_delay) {
			if (pwr.is_charging) sleep_ticks += 60000; // check again in a minute
			else PWR_powerOff();
		}
	}
	
	return;
}
void PWR_fauxSleep(void) {
	GFX_clear(gfx.screen);
	PAD_reset();
	PWR_enterSleep();
	PWR_waitForWake();
	PWR_exitSleep();
	PAD_reset();
}

void PWR_disableAutosleep(void) {
	pwr.can_autosleep = 0;
}
void PWR_enableAutosleep(void) {
	pwr.can_autosleep = 1;
}
int PWR_preventAutosleep(void) {
	return pwr.is_charging || !pwr.can_autosleep || GetHDMI();
}

// updated by PWR_updateBatteryStatus()
int PWR_isCharging(void) {
	return pwr.is_charging;
}
int PWR_getBattery(void) { // 10-100 in 10-20% fragments
	return pwr.charge;
}

///////////////////////////////

// TODO: tmp? move to individual platforms or allow overriding like PAD_poll/PAD_wake?
int PLAT_setDateTime(int y, int m, int d, int h, int i, int s) {
	char cmd[512];
	sprintf(cmd, "date -s '%d-%d-%d %d:%d:%d'; hwclock --utc -w", y,m,d,h,i,s);
	system(cmd);
	return 0; // why does this return an int?
}
