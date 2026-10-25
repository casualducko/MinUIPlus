// Title art: makes a picture of each game's title, in big ASCII letters, for the launcher to show
// next to the game (it draws <Roms folder>/.res/<file name>.png). A game that already has art is
// never touched; the tiles made here are remembered in titleart-made.txt so they can be made again
// (after a theme change) without touching art that was put there by hand.
// A makes the missing ones, X makes the tool's own again, B quits (or cancels).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "titleart_core.h"
#include "titleart_gen.h"

#define MAX_GAMES 8192
#define MAX_MADE 8192

///////////////////////////////
// the games and what is made

typedef struct { uint8_t folder; char file[200]; uint8_t has_art; } Game;
static char folders[128][128];
static int folder_count = 0;
static Game* games = NULL;
static int game_count = 0;

typedef struct { long mtime; char path[MAX_PATH]; } Made;
static Made* made = NULL;
static int made_count = 0;

static int isGameFile(const char* name) {
	if (name[0]=='.') return 0;
	const char* dot = strrchr(name, '.');
	if (!dot || !dot[1]) return 0; // no extension, not a game
	static const char* skip[] = {"png","jpg","jpeg","bmp","gif","txt","cfg","sav","srm","bak","xml","db","dat","log","pdf","cht","ini","sh","st","state","rtc",NULL};
	for (int i=0; skip[i]; i++) if (!strcasecmp(dot+1, skip[i])) return 0;
	if (!strncasecmp(dot+1, "st", 2) && isdigit((unsigned char)dot[3])) return 0; // save states: .st0
	return 1;
}
static void artPath(const Game* g, char* out, size_t size) {
	snprintf(out, size, ROMS_PATH "/%s/.res/%s.png", folders[g->folder], g->file);
}
static long fileMtime(const char* path) {
	struct stat st;
	return stat(path, &st)==0 ? (long)st.st_mtime : -1;
}
static void scanGames(uint64_t deadline_us) { // deadline_us: stop looking at this time (getMicroseconds), 0 for no limit
	free(games);
	games = calloc(MAX_GAMES, sizeof(Game));
	game_count = 0;
	folder_count = 0;
	if (!games) return;
	DIR* roms = opendir(ROMS_PATH);
	if (!roms) return;
	struct dirent* fe;
	while ((fe = readdir(roms)) && folder_count<128) {
		if (fe->d_name[0]=='.') continue;
		if (strlen(fe->d_name)>=sizeof(folders[0])) continue; // a name that doesn't fit would be kept cut short, and its pictures put in a folder that is not there
		if (deadline_us && getMicroseconds()>deadline_us) break;
		char dir[MAX_PATH];
		snprintf(dir, sizeof(dir), ROMS_PATH "/%s", fe->d_name);
		struct stat st;
		if (stat(dir, &st) || !S_ISDIR(st.st_mode)) continue;
		DIR* d = opendir(dir);
		if (!d) continue;
		int fi = folder_count++;
		snprintf(folders[fi], sizeof(folders[fi]), "%s", fe->d_name);
		struct dirent* e;
		while ((e = readdir(d)) && game_count<MAX_GAMES) {
			if (deadline_us && (game_count & 63)==0 && getMicroseconds()>deadline_us) break;
			if (!isGameFile(e->d_name) || strlen(e->d_name)>=sizeof(games[0].file)) continue;
			char full[MAX_PATH];
			snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
			if (stat(full, &st) || !S_ISREG(st.st_mode)) continue;
			Game* g = &games[game_count++];
			g->folder = fi;
			snprintf(g->file, sizeof(g->file), "%s", e->d_name);
			char art[MAX_PATH];
			artPath(g, art, sizeof(art));
			g->has_art = exists(art);
		}
		closedir(d);
	}
	closedir(roms);
}
static int madeIndex(const char* path) {
	for (int i=0; i<made_count; i++) if (!strcmp(made[i].path, path)) return i;
	return -1;
}
static void loadMade(void) {
	free(made);
	made = calloc(MAX_MADE, sizeof(Made));
	made_count = 0;
	if (!made) return;
	char* text = allocFile(TAGEN_MADE_PATH);
	if (!text) return;
	char* save = NULL;
	for (char* line = strtok_r(text, "\n", &save); line && made_count<MAX_MADE; line = strtok_r(NULL, "\n", &save)) {
		char* tab = strchr(line, '\t');
		if (!tab || !tab[1]) continue;
		*tab = '\0';
		int at = madeIndex(tab+1);
		if (at<0) { at = made_count++; snprintf(made[at].path, sizeof(made[at].path), "%s", tab+1); }
		made[at].mtime = atol(line); // a path can be there more than once: the last line is the newest
	}
	free(text);
}
static void saveMade(void) {
	size_t cap = (size_t)made_count * (MAX_PATH + 24) + 16;
	char* text = malloc(cap);
	if (!text) return;
	size_t len = 0;
	text[0] = '\0';
	for (int i=0; i<made_count; i++) {
		if (!exists(made[i].path)) continue; // the file was deleted
		len += snprintf(text+len, cap-len, "%ld\t%s\n", made[i].mtime, made[i].path);
	}
	putFile(TAGEN_MADE_PATH, text);
	free(text);
}
static int isMine(int i) { // made here and not replaced since (a FAT clock is 2 seconds wide)
	long now = fileMtime(made[i].path);
	return now>=0 && labs(now - made[i].mtime) <= 2;
}

typedef struct { char art[MAX_PATH]; char file[200]; char folder[128]; } Job;

///////////////////////////////
// drawing

static void text(SDL_Surface* screen, TTF_Font* f, const char* s, SDL_Color color, int x, int y, int max_w) {
	if (!s || !*s) return;
	SDL_Surface* t = TTF_RenderUTF8_Blended(f, s, color);
	if (!t) return;
	SDL_BlitSurface(t, &(SDL_Rect){0,0,MIN(t->w, max_w),t->h}, screen, &(SDL_Rect){x,y});
	SDL_FreeSurface(t);
}

enum { ST_HOME, ST_WORK, ST_DONE };

// titleart.elf --auto: no screen. Makes the pictures that new games are missing, for a few seconds at
// most (the next boot continues), and only when the game art is switched on. MinUI.pak/launch.sh runs
// it once at boot when Tools > Settings > "Art for new games" is Automatic.
#define AUTO_MAX_US 4000000
#define AUTO_MAX_TILES 60
static int autoMain(void) {
	if (!TAGen_showArt()) return 0;
	SDL_Init(0);
	char text[2048] = "";
	FILE* f = fopen(USERDATA_PATH "/theme.cfg", "r"); // there is no GFX_init here to read the colors
	if (f) {
		size_t n = fread(text, 1, sizeof(text)-1, f);
		text[n] = '\0';
		fclose(f);
		char font_unused[MAX_PATH] = "";
		THEME_parse(text, &theme, font_unused, sizeof(font_unused));
	}
	if (TAGen_open()) { LOG_error("titleart: can't open the font\n"); return 1; }
	uint64_t start = getMicroseconds(); // the budget counts from here, looking at the card included
	scanGames(start + AUTO_MAX_US);
	int made_now = 0, failed = 0, missing = 0;
	for (int i=0; i<game_count; i++) if (!games[i].has_art) missing++;
	for (int i=0; i<game_count && made_now<AUTO_MAX_TILES; i++) {
		if (games[i].has_art) continue;
		if (getMicroseconds() - start > AUTO_MAX_US) break;
		char rom[MAX_PATH];
		snprintf(rom, sizeof(rom), ROMS_PATH "/%s/%s", folders[games[i].folder], games[i].file);
		int r = TAGen_forRom(rom);
		if (r>0) made_now++;
		else if (r<0) failed++;
	}
	LOG_info("titleart --auto: %i of %i games without art done, %i failed, %llu ms\n", made_now, missing, failed, (unsigned long long)((getMicroseconds() - start) / 1000));
	TAGen_close();
	free(games);
	return 0;
}

int main(int argc, char* argv[]) {
	if (argc>1 && !strcmp(argv[1], "--auto")) return autoMain();
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();
	if (TAGen_open()) LOG_error("titleart: can't open the font\n");
	loadMade();
	scanGames(0);

	int row_h = SCALE1(PILL_SIZE);
	int w = screen->w - SCALE1(PADDING*2);
	int quit = 0, dirty = 1, show_setting = 0;
	int state = ST_HOME;
	Job* jobs = NULL;
	int job_count = 0, job_done = 0, job_failed = 0;
	char note[160] = "";

	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		char art_flag[8] = "";
		getFile(SHOW_ART_PATH, art_flag, sizeof(art_flag));
		int art_shown = art_flag[0]!='0';
		int missing = 0, mine = 0;
		for (int i=0; i<game_count; i++) if (!games[i].has_art) missing++;
		for (int i=0; i<made_count; i++) if (isMine(i)) mine++;

		if (state==ST_HOME) {
			if (PAD_justPressed(BTN_B)) quit = 1;
			else if (PAD_justPressed(BTN_Y)) { // show or hide the game art in the launcher
				art_shown = !art_shown;
				putFile(SHOW_ART_PATH, art_shown ? "1\n" : "0\n");
				dirty = 1;
			}
			else if (PAD_justPressed(BTN_X) && (missing>0 || mine>0)) { // every game without art, and this tool's tiles again
				free(jobs);
				jobs = calloc(MAX_MADE + MAX_GAMES, sizeof(Job));
				job_count = job_done = job_failed = 0;
				if (jobs) {
					for (int i=0; i<game_count; i++) {
						if (games[i].has_art) continue;
						Job* j = &jobs[job_count++];
						artPath(&games[i], j->art, sizeof(j->art));
						snprintf(j->file, sizeof(j->file), "%s", games[i].file);
						snprintf(j->folder, sizeof(j->folder), "%s", folders[games[i].folder]);
					}
					for (int i=0; i<made_count && job_count<MAX_MADE; i++) {
						if (!isMine(i)) continue;
						Job* j = &jobs[job_count++];
						snprintf(j->art, sizeof(j->art), "%s", made[i].path);
						char* res = strstr(j->art, "/.res/");
						if (!res) { job_count--; continue; }
						snprintf(j->file, sizeof(j->file), "%s", res + 6);
						char* ext = strrchr(j->file, '.'); // the .png
						if (ext) *ext = '\0';
						char dir[MAX_PATH];
						snprintf(dir, sizeof(dir), "%.*s", (int)(res - j->art), j->art);
						char* slash = strrchr(dir, '/');
						snprintf(j->folder, sizeof(j->folder), "%s", slash ? slash+1 : dir);
					}
				}
				if (job_count>0) { state = ST_WORK; PWR_disableAutosleep(); }
				dirty = 1;
			}
		}
		else if (state==ST_WORK) {
			if (PAD_justPressed(BTN_B) || job_done>=job_count) {
				int cancelled = job_done<job_count;
				saveMade();
				PWR_enableAutosleep();
				int made_ok = job_done - job_failed;
				snprintf(note, sizeof(note), "%s %i tile%s.", cancelled ? "Stopped after" : "Made", made_ok, made_ok==1 ? "" : "s");
				if (job_failed) snprintf(note + strlen(note), sizeof(note) - strlen(note), "\n%i failed.", job_failed);
				state = ST_DONE;
				scanGames(0);
				dirty = 1;
			}
			else {
				Job* j = &jobs[job_done];
				char dir[MAX_PATH];
				snprintf(dir, sizeof(dir), "%s", j->art);
				char* slash = strrchr(dir, '/');
				if (slash) *slash = '\0';
				char badge[96];
				TA_badge(j->folder, badge, sizeof(badge));
				int skip = exists(j->art) && madeIndex(j->art)<0; // a picture that appeared since the scan is somebody's: left alone
				int ok = skip ? 0 : TAGen_write(j->file, badge, j->art)==0;
				if (skip) {}
				else if (ok) {
					long mt = fileMtime(j->art);
					int mi = madeIndex(j->art);
					if (mi<0 && made_count<MAX_MADE) { mi = made_count++; snprintf(made[mi].path, sizeof(made[mi].path), "%s", j->art); }
					if (mi>=0) made[mi].mtime = mt;
				}
				else job_failed++;
				job_done++;
				dirty = 1;
			}
		}
		else if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) { state = ST_HOME; dirty = 1; }

		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			int tx = SCALE1(PADDING+BUTTON_PADDING);
			char out[160];
			GFX_truncateText(font.large, "ASCIIArt", out, screen->w - SCALE1(PADDING*2) - ow, SCALE1(BUTTON_PADDING*2));
			text(screen, font.large, out, COLOR_LIGHT_TEXT, tx, SCALE1(PADDING)+4, screen->w);
			int y = SCALE1(PADDING) + row_h + SCALE1(8);
			if (state==ST_HOME) {
				char line[160];
				snprintf(line, sizeof(line), "%i game%s without art", missing, missing==1 ? "" : "s");
				text(screen, font.large, line, COLOR_WHITE, tx, y, w);
				snprintf(line, sizeof(line), "%i tile%s made by this tool", mine, mine==1 ? "" : "s");
				text(screen, font.large, line, COLOR_WHITE, tx, y + row_h, w);
				text(screen, font.small, "A picture of each game's title for the launcher.", COLOR_GRAY, tx, y + row_h*2 + SCALE1(8), w);
				text(screen, font.small, "Art from elsewhere is never replaced.", COLOR_GRAY, tx, y + row_h*2 + SCALE1(26), w);
				snprintf(line, sizeof(line), "Art in the launcher: %s", art_shown ? "On" : "Off");
				text(screen, font.large, line, art_shown ? COLOR_WHITE : COLOR_LIGHT_TEXT, tx, y + row_h*4 + SCALE1(8), w);
				if (!missing && !mine) text(screen, font.small, "Every game has art.", COLOR_LIGHT_TEXT, tx, y + row_h*2 + SCALE1(44), w);
				GFX_blitButtonGroup((char*[]){"B","BACK","Y",art_shown ? "ART ON" : "ART OFF",NULL}, 0, screen, 0);
				if (missing>0 || mine>0) GFX_blitButtonGroup((char*[]){"X","RECREATE ALL",NULL}, 1, screen, 1);
			}
			else if (state==ST_WORK) {
				int bw = w - SCALE1(BUTTON_PADDING*2);
				int by = screen->h/2 - row_h;
				Job* j = &jobs[MIN(job_done, job_count-1)];
				char title[256], shown[256];
				TA_title(j->file, title, sizeof(title));
				GFX_truncateText(font.large, title, shown, bw, 0);
				text(screen, font.large, shown, COLOR_WHITE, tx, by, bw);
				char line[96];
				snprintf(line, sizeof(line), "%i of %i", job_done, job_count);
				text(screen, font.small, line, COLOR_LIGHT_TEXT, tx, by + row_h, bw);
				int bar_y = by + row_h*2;
				GFX_blitPill(ASSET_BAR_BG_MENU, screen, &(SDL_Rect){tx, bar_y, bw, SCALE1(SETTINGS_SIZE)});
				if (job_done>0) GFX_blitPill(ASSET_BAR, screen, &(SDL_Rect){tx, bar_y, MAX(bw * job_done / job_count, SCALE1(SETTINGS_SIZE)), SCALE1(SETTINGS_SIZE)});
				GFX_blitButtonGroup((char*[]){"B","STOP",NULL}, 0, screen, 1);
			}
			else {
				GFX_blitMessage(font.large, note, screen, &(SDL_Rect){0, SCALE1(PADDING), screen->w, screen->h - SCALE1(PILL_SIZE+PADDING)});
				GFX_blitButtonGroup((char*[]){"A","OKAY",NULL}, 1, screen, 1);
			}
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

	TAGen_close();
	free(jobs);
	free(games);
	free(made);
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
