// Pico Search: search the Lexaloffle BBS (PICO-8 carts), pick one and download its .p8.png into the PICO-8
// ROM folder so it shows up in the launcher. The BBS's own listing is fetched a few pages at a time and
// ranked here (a title that has your words comes before a post that only mentions them).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <msettings.h>

#include "sdl.h"
#include "defines.h"
#include "api.h"
#include "utils.h"
#include "bbs.h"
#include "titleart_gen.h"

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#define CFG_PATH		USERDATA_PATH "/pico.cfg"			// folder=Roms/Pico-8 (P8-NATIVE)  (optional)
#define PLAY_NEXT_PATH	"/tmp/pico-play" // the launch script runs this after the tool quits
#define DOWNLOAD_TMP	USERDATA_PATH "/pico-download.tmp"
#define MAX_PAGES		3 // pages of 30 fetched for one list
#define MAX_CARTS		(BBS_PAGE_SIZE * MAX_PAGES)
#define NOPREVIEW_PATH	USERDATA_PATH "/pico-no-preview"	// (an older version's: it meant "list only")
#define VIEW_PATH		USERDATA_PATH "/pico-view"			// 0 list with picture, 1 list, 2 thumbnail grid
#define LEDGER_PATH		USERDATA_PATH "/pico-downloaded.txt" // "<thread>\t<file name>" for each cart downloaded

typedef enum { SCR_HOME, SCR_TAGS, SCR_LIST, SCR_OSK } Screen;
typedef enum { JOB_NONE, JOB_PAGE, JOB_THREAD, JOB_CART } JobKind;
typedef enum { MODAL_NONE, MODAL_MESSAGE, MODAL_CONFIRM, MODAL_PLAY } ModalKind;

static SDL_Surface* screen;
static int dirty = 1;
static int quit = 0;
static int show_setting = 0;

// the listing
static char query[64];
static BbsCart carts[MAX_CARTS];
static int cart_count = 0;
static int sel = 0, scroll = 0;
static BbsSort sort_mode = BBS_SORT_BEST;
static int pages_loaded = 0;
static int exhausted = 0;
static int fetch_seq = 0; // carts counted as they come in, for "as listed"
// what the list shows: a search, or one of the BBS's own lists
static int list_is_search = 1;
static BbsBrowse browse_kind = BBS_BROWSE_FEATURED;
static char browse_tag[32];
static Screen list_back = SCR_HOME; // where B goes from a browse list
static int home_sel = 0;
static int tag_sel = 0, tag_scroll = 0;
static int page_failed = 0;

// on-screen keyboard
static const char* osk_rows[4] = { "abcdefghij", "klmnopqrst", "uvwxyz0123", "456789.-'&" };
static int osk_x = 0, osk_y = 0;
static char osk_text[64];

static Screen cur_screen = SCR_HOME;

// background work
static BbsJob job;
static JobKind job_kind = JOB_NONE;
static char list_tmp[512];
// the preview picture of the selected cart: fetched a moment after the cursor stops, one at a time, kept in a
// folder of its own that goes when the tool does
#define THUMB_DELAY_MS 250
#define THUMB_JOBS 3 // fetched side by side (they are small)
static struct { BbsJob job; int on; long tid; char path[600]; } tj[THUMB_JOBS];
static char thumb_dir[512];
#define THUMB_CACHE 32 // pictures kept scaled for the screen, by cart and size; the oldest goes first
static struct { long tid; int w, h; SDL_Surface* surf; } tcache[THUMB_CACHE];
static int tcache_next = 0;
typedef enum { VIEW_LIST_PIC, VIEW_LIST, VIEW_GRID, VIEW_COUNT } ViewMode; // SELECT goes through them
static int view_mode = VIEW_LIST_PIC;
#define show_thumb (view_mode==VIEW_LIST_PIC)
static int grid_top = 0; // the first row of cells shown, in the grid view
static int last_sel = -1;
static uint32_t sel_changed_at = 0;
static BbsCart dl_cart;			// the cart being downloaded
static char dl_path[256];		// its file on the BBS
static uint32_t dl_drawn_at = 0;

// modal
static ModalKind modal = MODAL_NONE;
static char modal_msg[320];
static uint32_t modal_until = 0;

///////////////////////////////
// where the carts go

static int dir_exists(const char* path) { struct stat st; return stat(path, &st)==0 && S_ISDIR(st.st_mode); }

// The ROM folder for carts, relative to the card: pico.cfg's folder=, else an existing "(P8-NATIVE)" folder, else an
// existing "(P8)" one, else a new one named for the emulator this card has (native PICO-8 when its pak and binary are there).
static void dest_dir(char* dir, size_t cap) {
	char* cfg = allocFile(CFG_PATH);
	if (cfg) {
		for (char* line = strtok(cfg, "\r\n"); line; line = strtok(NULL, "\r\n")) {
			while (*line==' ' || *line=='\t') line++;
			if (!strncmp(line, "folder=", 7) && line[7] && strstr(line+7, "..")==NULL) {
				const char* f = line + 7;
				while (*f=='/') f++;
				snprintf(dir, cap, "%s/%s", SDCARD_PATH, f);
				free(cfg);
				return;
			}
		}
		free(cfg);
	}
	static const char* tags[] = { "(P8-NATIVE)", "(P8)" };
	for (int t=0; t<2; t++) {
		DIR* d = opendir(ROMS_PATH);
		if (!d) break;
		struct dirent* e;
		while ((e = readdir(d))) {
			if (e->d_name[0]=='.' || !strstr(e->d_name, tags[t])) continue;
			char path[MAX_PATH];
			snprintf(path, sizeof(path), "%s/%s", ROMS_PATH, e->d_name);
			if (dir_exists(path)) { snprintf(dir, cap, "%s", path); closedir(d); return; }
		}
		closedir(d);
	}
	int native = (exists(SDCARD_PATH "/Emus/" PLATFORM "/P8-NATIVE.pak/launch.sh") || exists(SYSTEM_PATH "/paks/Emus/P8-NATIVE.pak/launch.sh"))
		&& dir_exists(SDCARD_PATH "/Tools/" PLATFORM "/Splore.pak/pico-8");
	snprintf(dir, cap, "%s/%s", ROMS_PATH, native ? "Pico-8 (P8-NATIVE)" : "Pico-8 (P8)");
}
// Is this cart on the card? Carts have the same titles (there are several "Celeste"), so the thread number is what
// counts: the ledger says which file came from which thread. A file named for the title that no ledger line claims
// for another thread (one downloaded before the ledger existed) counts too.
static int is_saved(const BbsCart* c, const char* ledger, const char* dir) {
	char file[256], path[MAX_PATH+256], name[256];
	bbs_file_name(c->title, NULL, name, sizeof(name));
	int claimed_by_other = 0;
	for (const char* line = ledger; line && *line; ) {
		const char* end = strchr(line, '\n');
		size_t n = end ? (size_t)(end - line) : strlen(line);
		long tid = atol(line);
		const char* tab = memchr(line, '\t', n);
		if (tab && tid>0) {
			snprintf(file, sizeof(file), "%.*s", (int)(n - (tab+1-line)), tab+1);
			if (tid==c->tid) {
				snprintf(path, sizeof(path), "%s/%s", dir, file);
				if (exists(path)) return 1;
			}
			else if (!strcmp(file, name)) claimed_by_other = 1;
		}
		line += n + (end ? 1 : 0);
	}
	if (claimed_by_other) return 0;
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	return exists(path);
}
static void refresh_saved(void) { // once per page and download, not on every redraw
	char dir[MAX_PATH];
	dest_dir(dir, sizeof(dir));
	char* ledger = allocFile(LEDGER_PATH);
	for (int i=0; i<cart_count; i++) carts[i].saved = is_saved(&carts[i], ledger ? ledger : "", dir);
	free(ledger);
}
static void ledger_add(long tid, const char* file) {
	FILE* f = fopen(LEDGER_PATH, "a");
	if (!f) return;
	fprintf(f, "%ld\t%s\n", tid, file);
	fclose(f);
}

///////////////////////////////
// playing a cart that was just downloaded (the same way the Romulus tool does it)

static char play_cmd[MAX_PATH*2+64];
static char play_rom[MAX_PATH];
static int play_native = 0; // the emulator does not record play time itself
static int make_play_cmd(const char* rom_path) { // 1 if the folder has an emulator on the card
	char emu_name[MAX_PATH], emu_path[MAX_PATH];
	getEmuName(rom_path, emu_name);
	getEmuPath(emu_name, emu_path);
	if (!emu_path[0] || !exists(emu_path)) return 0;
	char a[MAX_PATH*2], b[MAX_PATH*2];
	shellQuote(emu_path, a, sizeof(a));
	shellQuote(rom_path, b, sizeof(b));
	snprintf(play_cmd, sizeof(play_cmd), "%s %s\n", a, b);
	snprintf(play_rom, sizeof(play_rom), "%s", rom_path);
	play_native = Emu_isNative(emu_path);
	return 1;
}
static void start_play(void) {
	putFile(PLAY_NEXT_PATH, play_cmd);
	Recents_add(play_rom);
	if (play_native) Playtime_save(PLAYTIME_PATH, play_rom, 0, 1, (long)time(NULL)); // so its New dot goes
	putFile(LAST_PATH, play_rom);
	putInt(RESUME_SLOT_PATH, 8);
	quit = 1;
}

static void say(const char* msg, int ms) { // ms==0: until a button is pressed
	snprintf(modal_msg, sizeof(modal_msg), "%s", msg);
	modal = MODAL_MESSAGE;
	modal_until = ms ? SDL_GetTicks()+ms : 0;
	dirty = 1;
}
static void stop_job(void) {
	if (job_kind!=JOB_NONE) {
		bbs_job_cancel(&job);
		bbs_job_cleanup(&job);
		if (job_kind==JOB_CART) unlink(DOWNLOAD_TMP);
		job_kind = JOB_NONE;
		PWR_enableAutosleep();
	}
}

///////////////////////////////
// the network (as in the Romulus tool: switch Wi-Fi on if it is off, then wait for an address)

#define WIFI_TIMEOUT_MS 45000
#define NET_RETRIES 3
typedef enum { NET_READY, NET_ENABLING, NET_WAITING } NetState;
static NetState net_state = NET_READY;
static uint32_t net_deadline = 0;
static uint32_t net_drawn_at = 0;
static pid_t wifictl_pid = 0;

static const char* net_dir(void) { const char* e = getenv("PICO_NET_DIR"); return (e && *e) ? e : "/sys/class/net"; }
static int net_managed(void) { return access(net_dir(), F_OK)==0; } // no /sys/class/net: not the device
static int wifi_link_up(void) {
	char path[256], state[32] = "";
	snprintf(path, sizeof(path), "%s/wlan0/operstate", net_dir());
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	if (!fgets(state, sizeof(state), f)) state[0] = '\0';
	fclose(f);
	return !strncmp(state, "up", 2);
}
static int network_ready(void) { // a default route, by whatever interface
	const char* e = getenv("PICO_ROUTE_FILE");
	FILE* f = fopen((e && *e) ? e : "/proc/net/route", "r");
	if (!f) return 0;
	char line[256];
	int found = 0;
	while (fgets(line, sizeof(line), f)) {
		char iface[32], dest[16];
		if (sscanf(line, "%31s %15s", iface, dest)==2 && !strcmp(dest, "00000000")) { found = 1; break; }
	}
	fclose(f);
	return found;
}
static void start_wifictl(void) {
	const char* cmd = getenv("PICO_WIFICTL");
	if (!cmd || !*cmd) cmd = "wifiup"; // wifictl enable, then the other saved networks
	pid_t pid = fork();
	if (pid==0) {
		int nul = open("/dev/null", O_RDWR);
		if (nul>=0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
		for (int fd=3; fd<256; fd++) close(fd);
		execlp(cmd, cmd, "enable", (char*)NULL);
		_exit(127);
	}
	if (pid>0) wifictl_pid = pid;
}
static void reap_wifictl(void) {
	if (wifictl_pid>0 && waitpid(wifictl_pid, NULL, WNOHANG)!=0) wifictl_pid = 0;
}

///////////////////////////////
// jobs

static char* slurp(const char* path, size_t* len) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n<=0 || n>8*1024*1024) { fclose(f); return NULL; }
	char* b = malloc(n+1);
	if (b && fread(b, 1, n, f)!=(size_t)n) { free(b); b = NULL; }
	fclose(f);
	if (b) { b[n] = '\0'; *len = n; }
	return b;
}

static void load_next_page(void) {
	char url[1024];
	if (list_is_search) bbs_search_url(url, sizeof(url), query, pages_loaded+1);
	else bbs_browse_url(url, sizeof(url), browse_kind, browse_tag, pages_loaded+1);
	if (bbs_job_start(&job, url, list_tmp, 0)) { page_failed = 1; say("Couldn't start curl", 0); return; }
	job_kind = JOB_PAGE;
	page_failed = 0;
	dirty = 1;
}
static void new_search(void) {
	stop_job();
	cart_count = 0;
	sel = scroll = grid_top = 0;
	pages_loaded = 0;
	exhausted = 0;
	page_failed = 0;
	fetch_seq = 0;
	sort_mode = list_is_search ? BBS_SORT_BEST : BBS_SORT_LISTED;
	cur_screen = SCR_LIST;
	load_next_page();
}
static void finish_page(void) {
	const char* err = bbs_job_error(&job);
	if (err) { page_failed = 1; say(err, cart_count ? 2500 : 0); return; } // (a later page that fails can be asked for again with A)
	size_t len = 0;
	char* body = slurp(list_tmp, &len);
	BbsCart* got = malloc(sizeof(BbsCart) * BBS_PAGE_SIZE * 2);
	int n = (body && got) ? bbs_parse_search(body, len, got, BBS_PAGE_SIZE * 2) : -1;
	free(body);
	if (n<0) { free(got); page_failed = 1; say("The BBS page made no sense", cart_count ? 2500 : 0); return; }
	char keep[16] = "";
	if (sel>=0 && sel<cart_count) snprintf(keep, sizeof(keep), "%s", carts[sel].pid);
	for (int i=0; i<n && cart_count<MAX_CARTS; i++) {
		int dup = 0;
		for (int k=0; k<cart_count; k++) if (carts[k].tid==got[i].tid) { dup = 1; break; }
		if (!dup) { got[i].order = fetch_seq++; carts[cart_count++] = got[i]; }
	}
	free(got);
	bbs_sort(carts, cart_count, query, sort_mode);
	if (keep[0]) for (int i=0; i<cart_count; i++) if (!strcmp(carts[i].pid, keep)) { sel = i; break; }
	refresh_saved();
	pages_loaded++;
	if (n<BBS_PAGE_SIZE || pages_loaded>=MAX_PAGES || cart_count>=MAX_CARTS) exhausted = 1;
	if (!list_is_search && browse_kind==BBS_BROWSE_RANDOM) exhausted = 1; // a shuffle is one page; Y for another
}

static void start_cart_download(void) { // the thread page said where the cart is
	char url[512];
	bbs_cart_url(url, sizeof(url), dl_path);
	if (bbs_job_start(&job, url, DOWNLOAD_TMP, 1)) { PWR_enableAutosleep(); say("Couldn't start curl", 0); return; }
	job_kind = JOB_CART;
	dirty = 1;
}
static void finish_thread(void) {
	const char* err = bbs_job_error(&job);
	if (err) { PWR_enableAutosleep(); say(err, 0); return; }
	size_t len = 0;
	char* body = slurp(list_tmp, &len);
	int ok = body && bbs_parse_cart_path(body, len, dl_path, sizeof(dl_path))==0;
	free(body);
	if (!ok) { PWR_enableAutosleep(); say("That post has no cart file", 0); return; }
	start_cart_download();
}
static void finish_cart(void) {
	const char* err = bbs_job_error(&job);
	PWR_enableAutosleep();
	if (err) { unlink(DOWNLOAD_TMP); say(err, 0); return; }
	if (!bbs_is_png(DOWNLOAD_TMP)) { unlink(DOWNLOAD_TMP); say("That wasn't a cart file\n(a Wi-Fi login page?)", 0); return; }
	char dir[MAX_PATH];
	dest_dir(dir, sizeof(dir));
	if (mkdirs(dir)) { unlink(DOWNLOAD_TMP); say("Couldn't make the cart folder", 0); return; }
	char name[256], path[MAX_PATH+256];
	bbs_file_name(dl_cart.title, NULL, name, sizeof(name));
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (exists(path)) { // never replaced: a second copy gets the thread number
		char suffix[24];
		snprintf(suffix, sizeof(suffix), "(%ld)", dl_cart.tid);
		bbs_file_name(dl_cart.title, suffix, name, sizeof(name));
		snprintf(path, sizeof(path), "%s/%s", dir, name);
	}
	if (exists(path)) { unlink(DOWNLOAD_TMP); say("Already on the card", 2500); return; }
	if (rename(DOWNLOAD_TMP, path)) {
		char part[MAX_PATH+270];
		snprintf(part, sizeof(part), "%s/.pico-download.part", dir);
		if (copyFile(DOWNLOAD_TMP, part) || rename(part, path)) { remove(part); unlink(DOWNLOAD_TMP); say("Couldn't save the file (card full?)", 0); return; }
		unlink(DOWNLOAD_TMP);
	}
	sync();
	ledger_add(dl_cart.tid, name);
	refresh_saved();
	if (TAGen_showArt()) TAGen_forRom(path); // the new cart gets its title picture at once (see Tools > ASCIIArt)
	const char* folder = strrchr(dir, '/');
	char place[128];
	snprintf(place, sizeof(place), "%s", folder ? folder+1 : dir);
	char* tag = strstr(place, " (");
	if (tag) *tag = '\0';
	char msg[400];
	snprintf(msg, sizeof(msg), "Downloaded\n\n%s\n\nSaved to %s", dl_cart.title, place);
	if (make_play_cmd(path)) {
		snprintf(modal_msg, sizeof(modal_msg), "%s", msg);
		modal = MODAL_PLAY;
		modal_until = 0;
		dirty = 1;
	}
	else say(msg, 3000);
}

static void start_download(const BbsCart* c) {
	if (c!=&dl_cart) dl_cart = *c;
	char url[512];
	bbs_thread_url(url, sizeof(url), c->tid);
	stop_job();
	if (bbs_job_start(&job, url, list_tmp, 0)) { say("Couldn't start curl", 0); return; }
	job_kind = JOB_THREAD;
	PWR_disableAutosleep();
	dirty = 1;
}

///////////////////////////////
// drawing

#define ROW_H SCALE1(PILL_SIZE)

static int list_rows(void) { return MAIN_ROW_COUNT - 1; } // the first row is the title

static void blit_text(TTF_Font* ttf, const char* text, SDL_Color color, int x, int y, int max_w) {
	if (!text || !*text) return;
	SDL_Surface* s = GFX_renderText(ttf, text, color);
	if (!s) return;
	SDL_BlitSurface(s, &(SDL_Rect){0,0,MIN(s->w, max_w),s->h}, screen, &(SDL_Rect){x,y});
	SDL_FreeSurface(s);
}
static void draw_title(const char* text, int ow) {
	char out[256];
	int w = screen->w - SCALE1(PADDING*2) - ow;
	GFX_truncateText(font.large, text, out, w, SCALE1(BUTTON_PADDING*2));
	blit_text(font.large, out, COLOR_LIGHT_TEXT, SCALE1(PADDING+BUTTON_PADDING), SCALE1(PADDING)+4, w);
}
// a filled five-pointed star, size px across, by scanlines (no image needed, takes any colour)
static void draw_star(int x, int y, int size, SDL_Color c) {
	double cx = size/2.0, cy = size*0.53, R = size/2.0, r = R*0.4, px[10], py[10];
	for (int i=0; i<10; i++) {
		double a = -M_PI/2 + i*M_PI/5, rad = (i%2) ? r : R;
		px[i] = cx + rad*cos(a); py[i] = cy + rad*sin(a);
	}
	Uint32 col = SDL_MapRGB(screen->format, c.r, c.g, c.b);
	for (int row=0; row<size; row++) {
		double sy = row + 0.5, xs[10]; int n = 0;
		for (int i=0; i<10; i++) { // where the outline crosses this line
			int k = (i+1)%10;
			if ((py[i]<=sy && py[k]>sy) || (py[k]<=sy && py[i]>sy)) xs[n++] = px[i] + (sy-py[i])*(px[k]-px[i])/(py[k]-py[i]);
		}
		for (int a=0; a<n; a++) for (int b=a+1; b<n; b++) if (xs[b]<xs[a]) { double t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
		for (int a=0; a+1<n; a+=2) {
			int x0 = (int)(xs[a]+0.5), x1 = (int)(xs[a+1]+0.5);
			if (x1>x0) SDL_FillRect(screen, &(SDL_Rect){x+x0, y+row, x1-x0, 1}, col);
		}
	}
}
// a right-hand text that starts with \x01 is a star count: the number, then a star
#define STAR_MARK "\x01"
static void draw_row(int j, int selected, const char* text, const char* right, int dim) { // j counts from 0 below the title row
	int y = SCALE1(PADDING) + (j+1)*ROW_H;
	int w = screen->w - SCALE1(PADDING*2);
	SDL_Color color = dim ? COLOR_DARK_TEXT : COLOR_WHITE;
	int right_w = 0;
	SDL_Surface* rs = NULL;
	int star = right && right[0]==STAR_MARK[0];
	if (star) right++;
	int star_sz = TTF_FontHeight(font.small)*8/10, star_gap = SCALE1(4);
	if (right && *right) {
		rs = GFX_renderText(font.small, right, selected ? COLOR_BLACK : COLOR_DARK_TEXT);
		if (rs) right_w = rs->w + SCALE1(BUTTON_PADDING) + (star ? star_sz + star_gap : 0);
	}
	if (selected) {
		GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){SCALE1(PADDING), y, w, ROW_H});
		color = COLOR_BLACK;
	}
	char out[512];
	int avail = w - right_w;
	GFX_truncateText(font.large, text, out, avail, SCALE1(BUTTON_PADDING*2));
	blit_text(font.large, out, color, SCALE1(PADDING+BUTTON_PADDING), y+4, avail-SCALE1(BUTTON_PADDING*2));
	if (rs) {
		int right_x = SCALE1(PADDING)+w-SCALE1(BUTTON_PADDING);
		if (star) { right_x -= star_sz + star_gap; draw_star(right_x + star_gap, y+(ROW_H-star_sz)/2, star_sz, selected ? COLOR_BLACK : COLOR_DARK_TEXT); }
		SDL_BlitSurface(rs, NULL, screen, &(SDL_Rect){right_x-rs->w, y+(ROW_H-rs->h)/2});
		SDL_FreeSurface(rs);
	}
}
static void draw_note(const char* text) {
	GFX_blitMessage(font.large, (char*)text, screen, &(SDL_Rect){0, ROW_H, screen->w, screen->h-ROW_H*2});
}
static int cart_rows(void) { return list_rows() - (show_thumb ? 2 : 1); } // the cart list keeps its last row (two with the preview) for the selected cart's details
static void keep_visible(int* s, int* top, int count, int rows) {
	if (*s<0) *s = 0;
	if (*s>=count) *s = count>0 ? count-1 : 0;
	if (*s<*top) *top = *s;
	if (*s>=*top+rows) *top = *s-rows+1;
	if (*top<0) *top = 0;
}


///////////////////////////////
// the preview picture

#define THUMB_XRGB 0x00FF0000, 0x0000FF00, 0x000000FF, 0
static void thumb_file(long tid, char* out, size_t cap) { snprintf(out, cap, "%s/%ld.png", thumb_dir, tid); }
static void thumb_clear_dir(void) {
	if (!thumb_dir[0]) return;
	DIR* d = opendir(thumb_dir);
	if (d) {
		struct dirent* e;
		char p[640];
		while ((e = readdir(d))) {
			if (e->d_name[0]=='.') continue;
			snprintf(p, sizeof(p), "%s/%s", thumb_dir, e->d_name);
			unlink(p);
		}
		closedir(d);
	}
	rmdir(thumb_dir);
}
// the picture as large as a whole number of times fits the box (pixel art stays sharp); one that is too big is thinned out
static SDL_Surface* load_thumb(const char* path, int max_w, int max_h, int fill) { // fill: a small one may grow by a fraction too
	SDL_Surface* img = IMG_Load(path);
	if (!img || img->w<=0 || img->h<=0 || img->w>4096 || img->h>4096) { if (img) SDL_FreeSurface(img); return NULL; }
	SDL_Surface* src = SDL_CreateRGBSurface(SDL_SWSURFACE, img->w, img->h, 32, THUMB_XRGB);
	if (!src) { SDL_FreeSurface(img); return NULL; }
	SDL_BlitSurface(img, NULL, src, NULL);
	SDL_FreeSurface(img);
	int factor = MIN(max_w/src->w, max_h/src->h);
	int dw, dh;
	if (factor>=1 && !fill) { dw = src->w * factor; dh = src->h * factor; }
	else { double s = MIN((double)max_w/src->w, (double)max_h/src->h); dw = (int)(src->w * s); dh = (int)(src->h * s); }
	if (dw<1) dw = 1;
	if (dh<1) dh = 1;
	SDL_Surface* out = SDL_CreateRGBSurface(SDL_SWSURFACE, dw, dh, 32, THUMB_XRGB);
	if (!out) { SDL_FreeSurface(src); return NULL; }
	for (int y=0; y<dh; y++) {
		uint32_t* d = (uint32_t*)((uint8_t*)out->pixels + y*out->pitch);
		uint32_t* s = (uint32_t*)((uint8_t*)src->pixels + (long)(y*(long)src->h/dh)*src->pitch);
		for (int x=0; x<dw; x++) d[x] = s[(long)x*src->w/dw];
	}
	SDL_FreeSurface(src);
	return out;
}
static void thumb_forget(void) {
	for (int i=0; i<THUMB_CACHE; i++) { if (tcache[i].surf) SDL_FreeSurface(tcache[i].surf); tcache[i].surf = NULL; tcache[i].tid = 0; }
}
// the picture of a cart, scaled for a box of this size (NULL: not on disk yet, or not a picture)
static SDL_Surface* thumb_get(long tid, int bw, int bh, int fill) {
	for (int i=0; i<THUMB_CACHE; i++) if (tcache[i].surf && tcache[i].tid==tid && tcache[i].w==bw && tcache[i].h==bh) return tcache[i].surf;
	char path[640];
	thumb_file(tid, path, sizeof(path));
	SDL_Surface* t = load_thumb(path, bw, bh, fill);
	if (!t) return NULL;
	int k = tcache_next;
	tcache_next = (tcache_next+1) % THUMB_CACHE;
	if (tcache[k].surf) SDL_FreeSurface(tcache[k].surf);
	tcache[k].tid = tid; tcache[k].w = bw; tcache[k].h = bh; tcache[k].surf = t;
	return t;
}
static int thumb_box(void) { return ROW_H*2 + SCALE1(PADDING) - SCALE1(8); } // its height: two rows and the spare room under the list; it is a third wider (the BBS's pictures are 3:2 or square)

// the thumbnail grid: 5 across, a (square) picture over its title; 3 rows
#define GRID_COLS 5
#define GRID_GAP SCALE1(4)
typedef struct { int cell_w, thumb_h, cell_h, rows, x0, y0, info_y; } GridGeom;
static GridGeom grid_geom(void) {
	GridGeom g;
	int list_w = screen->w - SCALE1(PADDING*2);
	g.cell_w = (list_w - GRID_GAP*(GRID_COLS-1)) / GRID_COLS;
	int caption = TTF_FontHeight(font.small) + SCALE1(3);
	g.x0 = SCALE1(PADDING) + (list_w - (g.cell_w*GRID_COLS + GRID_GAP*(GRID_COLS-1)))/2;
	g.y0 = SCALE1(PADDING) + ROW_H + SCALE1(2);
	int bottom = screen->h - SCALE1(PADDING+PILL_SIZE) - SCALE1(6);
	g.info_y = bottom - SCALE1(13) - SCALE1(2); // one line under the grid
	// three rows, if the picture can be a little less than square to make them fit (most of the BBS's pictures are square)
	g.thumb_h = (g.info_y - g.y0 - GRID_GAP*2) / 3 - caption;
	if (g.thumb_h>g.cell_w) g.thumb_h = g.cell_w;
	if (g.thumb_h<g.cell_w*3/4) g.thumb_h = g.cell_w*3/4;
	g.cell_h = g.thumb_h + caption;
	g.rows = (g.info_y - g.y0 + GRID_GAP) / (g.cell_h + GRID_GAP);
	if (g.rows<1) g.rows = 1;
	return g;
}

static void thumb_finish(int k) { // a fetch has ended: remember how it went for the cart (by thread: the list may have been sorted since)
	const char* err = bbs_job_error(&tj[k].job);
	bbs_job_cleanup(&tj[k].job);
	tj[k].on = 0;
	int state = 1;
	if (err) state = 2;
	else {
		SDL_Surface* t = load_thumb(tj[k].path, 8, 8, 0); // (is it a picture at all?)
		if (!t) state = 2; else SDL_FreeSurface(t);
	}
	if (state==2) unlink(tj[k].path);
	for (int i=0; i<cart_count; i++) if (carts[i].tid==tj[k].tid) carts[i].thumb_state = state;
	dirty = 1;
}
static int thumb_busy(long tid) { for (int k=0; k<THUMB_JOBS; k++) if (tj[k].on && tj[k].tid==tid) return 1; return 0; }
static void thumb_start(BbsCart* c) {
	if (!c->thumb[0] || c->thumb_state!=0 || !thumb_dir[0] || thumb_busy(c->tid)) return;
	char path[600];
	thumb_file(c->tid, path, sizeof(path));
	if (access(path, F_OK)==0) { c->thumb_state = 1; dirty = 1; return; } // got it earlier in this visit
	int k = 0;
	while (k<THUMB_JOBS && tj[k].on) k++;
	if (k==THUMB_JOBS) return;
	char url[512];
	bbs_thumb_url(url, sizeof(url), c->thumb);
	snprintf(tj[k].path, sizeof(tj[k].path), "%s", path);
	if (bbs_job_start(&tj[k].job, url, tj[k].path, 0)) { c->thumb_state = 2; return; }
	tj[k].on = 1;
	tj[k].tid = c->tid;
}
static void thumb_tick(uint32_t now) { // fetch what is on the screen once the cursor has rested: the selected cart, or the cells of the grid
	for (int k=0; k<THUMB_JOBS; k++) if (tj[k].on && bbs_job_poll(&tj[k].job)) thumb_finish(k);
	if (sel!=last_sel) { last_sel = sel; sel_changed_at = now; }
	if (view_mode==VIEW_LIST || cur_screen!=SCR_LIST || modal!=MODAL_NONE || job_kind==JOB_CART || job_kind==JOB_THREAD || net_state!=NET_READY) return;
	if (sel<0 || sel>=cart_count || now-sel_changed_at<THUMB_DELAY_MS) return;
	thumb_start(&carts[sel]);
	if (view_mode==VIEW_GRID) {
		GridGeom g = grid_geom();
		int first = grid_top*GRID_COLS, last = first + g.rows*GRID_COLS;
		for (int i=first; i<last && i<cart_count; i++) thumb_start(&carts[i]);
	}
}

// the grid view: pictures in 4 columns, the selected one framed in white; under them who made the selected one
static void draw_grid(void) {
	GridGeom g = grid_geom();
	int first = grid_top*GRID_COLS;
	for (int j=0; j<g.rows*GRID_COLS && first+j<cart_count; j++) {
		int i = first + j;
		BbsCart* c = &carts[i];
		int x = g.x0 + (j%GRID_COLS)*(g.cell_w+GRID_GAP);
		int y = g.y0 + (j/GRID_COLS)*(g.cell_h+GRID_GAP);
		int selected = i==sel;
		if (selected) SDL_FillRect(screen, &(SDL_Rect){x-SCALE1(2), y-SCALE1(2), g.cell_w+SCALE1(4), g.cell_h+SCALE1(2)}, SDL_MapRGB(screen->format, 0xFF, 0xFF, 0xFF));
		SDL_Rect pic = {x, y, g.cell_w, g.thumb_h};
		SDL_FillRect(screen, &pic, SDL_MapRGB(screen->format, 0x18, 0x18, 0x18));
		SDL_Surface* t = c->thumb_state==1 ? thumb_get(c->tid, g.cell_w, g.thumb_h, 1) : NULL;
		if (t) SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){x+(g.cell_w-t->w)/2, y+(g.thumb_h-t->h)/2});
		else {
			SDL_Surface* m = GFX_renderText(font.small, c->thumb_state==2 || !c->thumb[0] ? "no picture" : "...", COLOR_GRAY);
			if (m) { SDL_BlitSurface(m, NULL, screen, &(SDL_Rect){x+(g.cell_w-m->w)/2, y+(g.thumb_h-m->h)/2}); SDL_FreeSurface(m); }
		}
		if (c->saved) { // a small label on the picture
			SDL_Surface* m = GFX_renderText(font.small, "SAVED", COLOR_WHITE);
			if (m) {
				SDL_FillRect(screen, &(SDL_Rect){x, y+g.thumb_h-m->h-SCALE1(2), m->w+SCALE1(6), m->h+SCALE1(2)}, SDL_MapRGB(screen->format, 0x00, 0x00, 0x00));
				SDL_BlitSurface(m, NULL, screen, &(SDL_Rect){x+SCALE1(3), y+g.thumb_h-m->h-SCALE1(1)});
				SDL_FreeSurface(m);
			}
		}
		char out[256];
		GFX_truncateText(font.small, c->title, out, g.cell_w, SCALE1(2));
		blit_text(font.small, out, selected ? COLOR_BLACK : COLOR_LIGHT_TEXT, x+SCALE1(1), y+g.thumb_h+SCALE1(1), g.cell_w);
	}
	if (sel>=0 && sel<cart_count) {
		BbsCart* c = &carts[sel];
		int w = screen->w - SCALE1(PADDING*2) - SCALE1(BUTTON_PADDING*2);
		char line[256], out[256];
		if (page_failed && sel==cart_count-1) snprintf(line, sizeof(line), "Couldn't load more. SELECT, then A to retry");
		else snprintf(line, sizeof(line), "%s  -  by %s  -  %d stars", c->title, c->author, c->stars);
		GFX_truncateText(font.small, line, out, w, 0);
		blit_text(font.small, out, COLOR_LIGHT_TEXT, SCALE1(PADDING+BUTTON_PADDING), g.info_y, w);
	}
}

static void draw_list(int ow) {
	char title[256];
	if (list_is_search) {
		if (query[0]) snprintf(title, sizeof(title), "\"%s\" - %s", query, bbs_sort_name(sort_mode));
		else snprintf(title, sizeof(title), "Recent - %s", bbs_sort_name(sort_mode));
	}
	else if (sort_mode==BBS_SORT_LISTED) snprintf(title, sizeof(title), "%s", bbs_browse_name(browse_kind, browse_tag));
	else snprintf(title, sizeof(title), "%s - %s", bbs_browse_name(browse_kind, browse_tag), bbs_sort_name(sort_mode));
	draw_title(title, ow);
	if (cart_count==0) {
		if (net_state==NET_ENABLING) draw_note("Turning on Wi-Fi...");
		else if (net_state==NET_WAITING) draw_note("Waiting for the network...");
		else if (job_kind==JOB_PAGE) draw_note("Searching...");
		else if (page_failed) draw_note("Couldn't load the list.\nPress A to try again.");
		else draw_note("No carts found.");
	}
	else if (view_mode==VIEW_GRID) draw_grid();
	else {
		int rows = cart_rows();
		int extra = exhausted ? 0 : 1; // a last row: more is loading, or the next page failed (A tries again)
		for (int j=0; j<rows && scroll+j<cart_count+extra; j++) {
			int i = scroll+j;
			if (i>=cart_count) { draw_row(j, i==sel, page_failed ? "Couldn't load more. Press A to retry" : "Loading more...", NULL, !page_failed); continue; }
			BbsCart* c = &carts[i];
			char right[80];
			if (c->saved) snprintf(right, sizeof(right), "SAVED");
			else snprintf(right, sizeof(right), STAR_MARK "%d", c->stars);
			draw_row(j, i==sel, c->title, right, c->saved && i!=sel);
		}
		if (sel>=0 && sel<cart_count) { // under the list: the selected cart's tags, then who made it and when (and its picture)
			BbsCart* c = &carts[sel];
			int list_w = screen->w - SCALE1(PADDING*2);
			int box = show_thumb ? thumb_box() : 0;
			int box_w = box*4/3;
			int w = list_w - SCALE1(BUTTON_PADDING*2) - (box ? box_w + SCALE1(BUTTON_PADDING) : 0);
			int y = SCALE1(PADDING) + (rows+1)*ROW_H + (show_thumb ? SCALE1(6) : 0); // (without the picture the text has less room above the buttons)
			char tags[200] = "", line[200], out[256];
			const char* p = c->tags;
			while (*p) { // "rpg, earthbound" -> "#rpg #earthbound"
				const char* e = strstr(p, ", ");
				size_t n = e ? (size_t)(e-p) : strlen(p);
				size_t t = strlen(tags);
				if (t+n+3<sizeof(tags)) snprintf(tags+t, sizeof(tags)-t, "%s#%.*s", t ? " " : "", (int)n, p);
				p += n + (e ? 2 : 0);
			}
			GFX_truncateText(font.small, tags[0] ? tags : "(no tags)", out, w, 0);
			blit_text(font.small, out, tags[0] ? COLOR_LIGHT_TEXT : COLOR_GRAY, SCALE1(PADDING+BUTTON_PADDING), y, w);
			if (box) { // one fact to a line beside the picture
				snprintf(line, sizeof(line), "by %s", c->author);
				GFX_truncateText(font.small, line, out, w, 0);
				blit_text(font.small, out, COLOR_GRAY, SCALE1(PADDING+BUTTON_PADDING), y + SCALE1(15), w);
				snprintf(line, sizeof(line), "%.10s  -  %d stars", c->date, c->stars);
				blit_text(font.small, line, COLOR_GRAY, SCALE1(PADDING+BUTTON_PADDING), y + SCALE1(30), w);
				blit_text(font.small, "SELECT: next view", COLOR_DARK_TEXT, SCALE1(PADDING+BUTTON_PADDING), y + SCALE1(45), w);
				SDL_Rect frame = {SCALE1(PADDING)+list_w-box_w, y-SCALE1(2), box_w, box};
				SDL_FillRect(screen, &frame, SDL_MapRGB(screen->format, 0x18, 0x18, 0x18));
				SDL_Surface* pic = c->thumb_state==1 ? thumb_get(c->tid, box_w, box, 0) : NULL;
				if (pic) SDL_BlitSurface(pic, NULL, screen, &(SDL_Rect){frame.x+(box_w-pic->w)/2, frame.y+(box-pic->h)/2});
				else {
					const char* msg = c->thumb_state==2 || !c->thumb[0] ? "no picture" : "...";
					SDL_Surface* m = GFX_renderText(font.small, msg, COLOR_GRAY);
					if (m) { SDL_BlitSurface(m, NULL, screen, &(SDL_Rect){frame.x+(box_w-m->w)/2, frame.y+(box-m->h)/2}); SDL_FreeSurface(m); }
				}
			}
			else {
				snprintf(line, sizeof(line), "by %s  -  %.10s  -  %d stars", c->author, c->date, c->stars);
				GFX_truncateText(font.small, line, out, w, 0);
				blit_text(font.small, out, COLOR_GRAY, SCALE1(PADDING+BUTTON_PADDING), y + SCALE1(13), w);
				blit_text(font.small, "SELECT: next view", COLOR_DARK_TEXT, SCALE1(PADDING+BUTTON_PADDING), y + SCALE1(26), w);
			}
		}
	}
	if (!list_is_search && browse_kind==BBS_BROWSE_RANDOM) GFX_blitButtonGroup((char*[]){"X","SEARCH","Y","SHUFFLE",NULL}, 0, screen, 0);
	else GFX_blitButtonGroup((char*[]){"X","SEARCH","Y","SORT",NULL}, 0, screen, 0);
	GFX_blitButtonGroup((char*[]){"B","BACK","A","GET",NULL}, 1, screen, 1);
}
static const struct { const char* label; const char* hint; } home_rows[] = {
	{"Search", "by name or word"}, {"Featured", "the BBS's picks"}, {"Popular now", "what is being played"},
	{"New and updated", ""}, {"Random", "a fresh shuffle"}, {"Browse by tag", "platformer, puzzle..."},
};
#define HOME_ROWS ((int)(sizeof(home_rows)/sizeof(home_rows[0])))
static void draw_home(int ow) {
	draw_title("PicoSearch", ow);
	for (int j=0; j<HOME_ROWS; j++) draw_row(j, j==home_sel, home_rows[j].label, home_rows[j].hint, 0);
	GFX_blitButtonGroup((char*[]){"B","QUIT",NULL}, 0, screen, 0);
	GFX_blitButtonGroup((char*[]){"A","OPEN",NULL}, 1, screen, 1);
}
static void draw_tags(int ow) {
	int count = 0;
	const char* const* tags = bbs_tag_list(&count);
	draw_title("Browse by tag", ow);
	for (int j=0; j<list_rows() && tag_scroll+j<count; j++) draw_row(j, tag_scroll+j==tag_sel, tags[tag_scroll+j], NULL, 0);
	GFX_blitButtonGroup((char*[]){"B","BACK",NULL}, 0, screen, 0);
	GFX_blitButtonGroup((char*[]){"A","OPEN",NULL}, 1, screen, 1);
}
static void draw_osk(void) {
	int w = screen->w - SCALE1(PADDING*2);
	GFX_blitPill(ASSET_DARK_GRAY_PILL, screen, &(SDL_Rect){SCALE1(PADDING), SCALE1(PADDING), w, ROW_H});
	char shown[80];
	snprintf(shown, sizeof(shown), "%s_", osk_text);
	blit_text(font.large, shown, COLOR_WHITE, SCALE1(PADDING+BUTTON_PADDING), SCALE1(PADDING)+4, w-SCALE1(BUTTON_PADDING*2));

	int cell_w = w / 10;
	int top = SCALE1(PADDING) + ROW_H*2;
	for (int r=0; r<4; r++) {
		for (int c=0; c<10; c++) {
			int x = SCALE1(PADDING) + c*cell_w;
			int y = top + r*ROW_H;
			int on = (osk_y==r && osk_x==c);
			if (on) GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){x+2, y, cell_w-4, ROW_H});
			char ch[2] = { osk_rows[r][c], 0 };
			SDL_Surface* s = GFX_renderText(font.large, ch, on ? COLOR_BLACK : COLOR_WHITE);
			if (s) { SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){x+(cell_w-s->w)/2, y+4}); SDL_FreeSurface(s); }
		}
	}
	const char* names[3] = { "SPACE", "DELETE", "SEARCH" };
	int starts[3] = { 0, 4, 7 }, widths[3] = { 4, 3, 3 };
	int cur = osk_x<4 ? 0 : (osk_x<7 ? 1 : 2);
	for (int b=0; b<3; b++) {
		int x = SCALE1(PADDING) + starts[b]*cell_w;
		int y = top + 4*ROW_H + ROW_H/2;
		int ww = widths[b]*cell_w - 4;
		int on = (osk_y==4 && cur==b);
		GFX_blitPill(on ? ASSET_WHITE_PILL : ASSET_DARK_GRAY_PILL, screen, &(SDL_Rect){x+2, y, ww, ROW_H});
		SDL_Surface* s = GFX_renderText(font.medium, names[b], on ? COLOR_BLACK : COLOR_WHITE);
		if (s) { SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){x+2+(ww-s->w)/2, y+(ROW_H-s->h)/2}); SDL_FreeSurface(s); }
	}
	GFX_blitButtonGroup((char*[]){"START","SEARCH",NULL}, 0, screen, 0);
	GFX_blitButtonGroup((char*[]){"B","DELETE","A","TYPE",NULL}, 1, screen, 1);
}
static void draw_progress(void) {
	int w = screen->w - SCALE1(PADDING*2) - SCALE1(BUTTON_PADDING*2);
	int x = (screen->w-w)/2;
	int y = screen->h/2 - ROW_H*2;
	char out[256];
	GFX_truncateText(font.large, dl_cart.title, out, w, 0);
	blit_text(font.large, out, COLOR_WHITE, x, y, w);
	char line[96];
	if (job_kind==JOB_THREAD) snprintf(line, sizeof(line), "Finding the cart...");
	else snprintf(line, sizeof(line), "%.0f KB", bbs_job_bytes(&job)/1024.0);
	blit_text(font.small, line, COLOR_LIGHT_TEXT, x, y+ROW_H, w);
	GFX_blitButtonGroup((char*[]){"B","CANCEL",NULL}, 0, screen, 1);
}
static void draw_modal(void) {
	int w = screen->w - SCALE1(PADDING*2) - SCALE1(BUTTON_PADDING*2);
	int h = ROW_H*5;
	int x = (screen->w-w)/2, y = (screen->h-h)/2;
	char text[320];
	snprintf(text, sizeof(text), "%s", modal_msg);
	if (!strchr(text, '\n')) GFX_wrapText(font.large, text, w, 6);
	GFX_blitMessage(font.large, text, screen, &(SDL_Rect){x, y, w, h});
	if (modal==MODAL_CONFIRM) GFX_blitButtonGroup((char*[]){"B","NO","A","YES",NULL}, 1, screen, 1);
	else if (modal==MODAL_PLAY) GFX_blitButtonGroup((char*[]){"B","BACK","A","PLAY",NULL}, 1, screen, 1);
	else if (!modal_until) GFX_blitButtonGroup((char*[]){"A","OK",NULL}, 1, screen, 1);
}

///////////////////////////////
// input

static int moved(int* s, int count, int step) {
	int before = *s;
	*s += step;
	if (*s<0) *s = (step==-1 && before==0) ? count-1 : 0;
	if (*s>=count) *s = (step==1 && before==count-1) ? 0 : count-1;
	return *s!=before;
}

static int net_wait_pending = 0; // a search is waiting for the network
static void begin_load(void) { // the list described by list_is_search, query, browse_kind and browse_tag (waits for the network if there is none yet)
	stop_job();
	if (!net_managed() || network_ready()) { net_wait_pending = 0; new_search(); return; }
	cart_count = 0;
	cur_screen = SCR_LIST;
	net_wait_pending = 1;
	const char* t = getenv("PICO_WIFI_TIMEOUT_MS");
	net_deadline = SDL_GetTicks() + (t ? (uint32_t)atoi(t) : WIFI_TIMEOUT_MS);
	if (wifi_link_up()) net_state = NET_WAITING;
	else { net_state = NET_ENABLING; start_wifictl(); }
	dirty = 1;
}

static void osk_sideways(int dx) { // left/right: a column on the letter rows; on the bottom row a whole button (SPACE, DELETE, SEARCH)
	if (osk_y==4) {
		static const int middle[3] = { 1, 5, 8 };
		int b = osk_x<4 ? 0 : (osk_x<7 ? 1 : 2);
		osk_x = middle[(b + (dx>0 ? 1 : 2)) % 3];
	}
	else osk_x = (osk_x + (dx>0 ? 1 : 9)) % 10;
}
static void do_osk_press(void) {
	size_t n = strlen(osk_text);
	if (osk_y<4) {
		if (n<sizeof(osk_text)-1) { osk_text[n] = osk_rows[osk_y][osk_x]; osk_text[n+1] = '\0'; }
		return;
	}
	int b = osk_x<4 ? 0 : (osk_x<7 ? 1 : 2);
	if (b==0) { if (n<sizeof(osk_text)-1) { osk_text[n] = ' '; osk_text[n+1] = '\0'; } }
	else if (b==1) { if (n) osk_text[n-1] = '\0'; }
	else {
		while (n && osk_text[n-1]==' ') osk_text[--n] = '\0';
		const char* t = osk_text;
		while (*t==' ') t++;
		snprintf(query, sizeof(query), "%s", t);
		list_is_search = 1;
		begin_load();
	}
}

static void handle_modal_input(void) {
	if (modal==MODAL_MESSAGE) {
		if (modal_until && SDL_GetTicks()>=modal_until) { modal = MODAL_NONE; dirty = 1; return; }
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) { modal = MODAL_NONE; dirty = 1; }
		return;
	}
	if (modal==MODAL_PLAY) {
		if (PAD_justPressed(BTN_A)) { modal = MODAL_NONE; start_play(); }
		else if (PAD_justPressed(BTN_B)) modal = MODAL_NONE;
		dirty = 1;
		return;
	}
	if (PAD_justPressed(BTN_A)) { modal = MODAL_NONE; start_download(&dl_cart); dirty = 1; } // (the only confirm is: another copy of dl_cart?)
	else if (PAD_justPressed(BTN_B)) { modal = MODAL_NONE; dirty = 1; }
}

static void handle_input(void) {
	if (job_kind==JOB_THREAD || job_kind==JOB_CART) { // only cancel
		if (PAD_justPressed(BTN_B)) { stop_job(); say("Cancelled", 1200); }
		return;
	}
	if (modal!=MODAL_NONE) { handle_modal_input(); return; }

	if (cur_screen==SCR_HOME) {
		if (PAD_justPressed(BTN_B)) { quit = 1; return; }
		if (PAD_justRepeated(BTN_DPAD_UP)) { moved(&home_sel, HOME_ROWS, -1); dirty = 1; }
		else if (PAD_justRepeated(BTN_DPAD_DOWN)) { moved(&home_sel, HOME_ROWS, 1); dirty = 1; }
		else if (PAD_justPressed(BTN_A)) {
			static const BbsBrowse kinds[] = { BBS_BROWSE_FEATURED, BBS_BROWSE_POPULAR, BBS_BROWSE_NEWEST, BBS_BROWSE_RANDOM }; // rows 1 to 4
			if (home_sel==0) { osk_x = osk_y = 0; cur_screen = SCR_OSK; }
			else if (home_sel==HOME_ROWS-1) cur_screen = SCR_TAGS;
			else { list_is_search = 0; browse_kind = kinds[home_sel-1]; browse_tag[0] = '\0'; list_back = SCR_HOME; begin_load(); }
			dirty = 1;
		}
		return;
	}
	if (cur_screen==SCR_TAGS) {
		int count = 0;
		const char* const* tags = bbs_tag_list(&count);
		if (PAD_justPressed(BTN_B)) { cur_screen = SCR_HOME; dirty = 1; return; }
		if (PAD_justRepeated(BTN_DPAD_UP)) { moved(&tag_sel, count, -1); dirty = 1; }
		else if (PAD_justRepeated(BTN_DPAD_DOWN)) { moved(&tag_sel, count, 1); dirty = 1; }
		else if (PAD_justRepeated(BTN_L1)) { moved(&tag_sel, count, -list_rows()); dirty = 1; }
		else if (PAD_justRepeated(BTN_R1)) { moved(&tag_sel, count, list_rows()); dirty = 1; }
		keep_visible(&tag_sel, &tag_scroll, count, list_rows());
		if (PAD_justPressed(BTN_A)) {
			list_is_search = 0;
			browse_kind = BBS_BROWSE_TAG;
			snprintf(browse_tag, sizeof(browse_tag), "%s", tags[tag_sel]);
			list_back = SCR_TAGS;
			begin_load();
			dirty = 1;
		}
		return;
	}
	if (cur_screen==SCR_OSK) {
		if (PAD_justPressed(BTN_DPAD_LEFT) || (osk_y<4 && PAD_justRepeated(BTN_DPAD_LEFT))) { osk_sideways(-1); dirty = 1; }
		else if (PAD_justPressed(BTN_DPAD_RIGHT) || (osk_y<4 && PAD_justRepeated(BTN_DPAD_RIGHT))) { osk_sideways(1); dirty = 1; }
		else if (PAD_justRepeated(BTN_DPAD_UP)) { osk_y = (osk_y+4)%5; dirty = 1; }
		else if (PAD_justRepeated(BTN_DPAD_DOWN)) { osk_y = (osk_y+1)%5; dirty = 1; }
		if (PAD_justPressed(BTN_A)) { do_osk_press(); dirty = 1; }
		else if (PAD_justRepeated(BTN_B)) {
			size_t n = strlen(osk_text);
			if (n) { osk_text[n-1] = '\0'; dirty = 1; }
			else if (PAD_justPressed(BTN_B)) { cur_screen = SCR_HOME; dirty = 1; } // nothing left to delete: back to the start
		}
		else if (PAD_justPressed(BTN_Y)) { osk_text[0] = '\0'; dirty = 1; }
		else if (PAD_justPressed(BTN_START)) { osk_y = 4; osk_x = 8; do_osk_press(); dirty = 1; }
		return;
	}

	// the list
	if (PAD_justPressed(BTN_B)) { // back to where the list came from
		stop_job(); net_state = NET_READY; net_wait_pending = 0;
		if (list_is_search) { snprintf(osk_text, sizeof(osk_text), "%s", query); cur_screen = SCR_OSK; }
		else cur_screen = list_back;
		dirty = 1;
		return;
	}
	if (PAD_justPressed(BTN_X) || PAD_justPressed(BTN_MENU)) { // MENU is the Fn button on this device
		stop_job(); net_state = NET_READY; net_wait_pending = 0; // (nothing may start the old search behind the keyboard)
		snprintf(osk_text, sizeof(osk_text), "%s", query);
		osk_x = osk_y = 0;
		cur_screen = SCR_OSK;
		dirty = 1;
		return;
	}
	if (PAD_justPressed(BTN_SELECT)) { // the next view: list with the picture, list, thumbnail grid (kept for next time)
		view_mode = (view_mode+1) % VIEW_COUNT;
		FILE* f = fopen(VIEW_PATH, "w");
		if (f) { fprintf(f, "%d\n", view_mode); fclose(f); }
		unlink(NOPREVIEW_PATH);
		grid_top = 0;
		if (view_mode==VIEW_GRID) { GridGeom g = grid_geom(); grid_top = sel/GRID_COLS - (g.rows-1)/2; if (grid_top<0) grid_top = 0; }
		else keep_visible(&sel, &scroll, cart_count + (exhausted ? 0 : 1), cart_rows());
		dirty = 1;
		return;
	}
	if (view_mode==VIEW_GRID && cart_count>0) { // the grid: left and right by one, up and down by a row, L1 and R1 by a page
		GridGeom g = grid_geom();
		int step = 0;
		if (PAD_justRepeated(BTN_DPAD_LEFT)) step = -1;
		else if (PAD_justRepeated(BTN_DPAD_RIGHT)) step = 1;
		else if (PAD_justRepeated(BTN_DPAD_UP)) step = -GRID_COLS;
		else if (PAD_justRepeated(BTN_DPAD_DOWN)) step = GRID_COLS;
		else if (PAD_justRepeated(BTN_L1)) step = -GRID_COLS*g.rows;
		else if (PAD_justRepeated(BTN_R1)) step = GRID_COLS*g.rows;
		if (step) {
			int to = sel + step;
			if (to<0) to = (step==-1 || sel==0) ? 0 : (sel%GRID_COLS); // (up from the first row stays; never wraps)
			if (to>=cart_count) to = (step==GRID_COLS || step==GRID_COLS*g.rows) ? cart_count-1 : (step==1 ? sel : cart_count-1);
			if (to!=sel) { sel = to; dirty = 1; }
		}
		int row = sel/GRID_COLS;
		if (row<grid_top) grid_top = row;
		if (row>=grid_top+g.rows) grid_top = row-g.rows+1;
	}
	if (PAD_justPressed(BTN_Y) && !list_is_search && browse_kind==BBS_BROWSE_RANDOM) { stop_job(); begin_load(); dirty = 1; return; } // another shuffle
	if (PAD_justPressed(BTN_Y) && cart_count>1) {
		if (list_is_search) sort_mode = sort_mode==BBS_SORT_BEST ? BBS_SORT_STARS : sort_mode==BBS_SORT_STARS ? BBS_SORT_NEWEST : BBS_SORT_BEST;
		else sort_mode = sort_mode==BBS_SORT_LISTED ? BBS_SORT_STARS : sort_mode==BBS_SORT_STARS ? BBS_SORT_NEWEST : BBS_SORT_LISTED;
		bbs_sort(carts, cart_count, query, sort_mode);
		sel = scroll = grid_top = 0; // the top of the new order is the point of changing it
		dirty = 1;
	}
	int extra = exhausted ? 0 : 1;
	int count = cart_count + extra;
	if (count>0 && view_mode!=VIEW_GRID) {
		if (PAD_justRepeated(BTN_DPAD_UP)) { moved(&sel, count, -1); dirty = 1; }
		else if (PAD_justRepeated(BTN_DPAD_DOWN)) { moved(&sel, count, 1); dirty = 1; }
		else if (PAD_justRepeated(BTN_L1)) { moved(&sel, count, -cart_rows()); dirty = 1; }
		else if (PAD_justRepeated(BTN_R1)) { moved(&sel, count, cart_rows()); dirty = 1; }
		keep_visible(&sel, &scroll, count, cart_rows());
	}
	if (PAD_justPressed(BTN_A)) {
		if (cart_count==0 || sel>=cart_count) { // nothing here, or the "more" row: A retries a page that failed
			if (job_kind==JOB_NONE && page_failed && net_state==NET_READY) { page_failed = 0; if (pages_loaded==0) new_search(); else load_next_page(); }
		}
		else if (carts[sel].saved) {
			dl_cart = carts[sel];
			snprintf(modal_msg, sizeof(modal_msg), "You already have this one.\nGet another copy?");
			modal = MODAL_CONFIRM;
		}
		else start_download(&carts[sel]);
		dirty = 1;
	}
}

///////////////////////////////

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	srand((unsigned)time(NULL));

	screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();

	const char* tmp = getenv("TMPDIR");
	snprintf(list_tmp, sizeof(list_tmp), "%s/pico-list-%d.html", (tmp && *tmp) ? tmp : "/tmp", (int)getpid());
	unlink(DOWNLOAD_TMP);
	{ // the view last used (an older version's "no preview" file meant the plain list)
		FILE* f = fopen(VIEW_PATH, "r");
		int v = -1;
		if (f) { if (fscanf(f, "%d", &v)!=1) v = -1; fclose(f); }
		if (v>=0 && v<VIEW_COUNT) view_mode = v;
		else if (access(NOPREVIEW_PATH, F_OK)==0) view_mode = VIEW_LIST;
	}
	snprintf(thumb_dir, sizeof(thumb_dir), "%s/pico-thumbs-%d", (tmp && *tmp) ? tmp : "/tmp", (int)getpid());
	if (mkdir(thumb_dir, 0700)!=0) thumb_dir[0] = '\0'; // no folder, no previews


	while (!quit) {
		GFX_startFrame();
		uint32_t now = SDL_GetTicks();

		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		reap_wifictl();
		if (net_state!=NET_READY) {
			if (network_ready() || now>=net_deadline) {
				net_state = NET_READY;
				if (net_wait_pending) { net_wait_pending = 0; new_search(); }
			}
			else {
				if (net_state==NET_ENABLING && wifi_link_up()) { net_state = NET_WAITING; dirty = 1; }
				if (now-net_drawn_at>=1000) { net_drawn_at = now; dirty = 1; }
			}
		}

		if (job_kind!=JOB_NONE && bbs_job_poll(&job)) {
			JobKind kind = job_kind;
			job_kind = JOB_NONE;
			bbs_job_cleanup(&job); // before the handler: finish_thread starts the cart's job, whose files these would be
			if (kind==JOB_PAGE) finish_page();
			else if (kind==JOB_THREAD) finish_thread();
			else if (kind==JOB_CART) finish_cart();
			dirty = 1;
		}
		// the rest of the search is fetched in the background while the list is being read
		if (job_kind==JOB_NONE && modal==MODAL_NONE && cur_screen==SCR_LIST && !exhausted && !page_failed && pages_loaded>0 && cart_count>0 && net_state==NET_READY) load_next_page();
		thumb_tick(now);
		if ((job_kind==JOB_CART || job_kind==JOB_THREAD) && now-dl_drawn_at>=150) { dirty = 1; dl_drawn_at = now; }

		handle_input();
		if (modal==MODAL_MESSAGE && modal_until && now>=modal_until) { modal = MODAL_NONE; dirty = 1; }

		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			if ((job_kind==JOB_CART || job_kind==JOB_THREAD) && modal==MODAL_NONE) draw_progress();
			else if (modal!=MODAL_NONE) draw_modal();
			else if (cur_screen==SCR_HOME) draw_home(ow);
			else if (cur_screen==SCR_TAGS) draw_tags(ow);
			else if (cur_screen==SCR_LIST) draw_list(ow);
			else draw_osk();
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

	stop_job();
	for (int k=0; k<THUMB_JOBS; k++) if (tj[k].on) { bbs_job_cancel(&tj[k].job); bbs_job_cleanup(&tj[k].job); }
	thumb_forget();
	thumb_clear_dir();
	unlink(list_tmp);
	TAGen_close();
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
