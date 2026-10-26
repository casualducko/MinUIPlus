// Pico Search: search the Lexaloffle BBS (PICO-8 carts), pick one and download its .p8.png into the PICO-8
// ROM folder so it shows up in the launcher. The BBS's own listing is fetched a few pages at a time and
// ranked here (a title that has your words comes before a post that only mentions them).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
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

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#define CFG_PATH		USERDATA_PATH "/pico.cfg"			// folder=Roms/Pico-8 (P8-NATIVE)  (optional)
#define PLAY_NEXT_PATH	"/tmp/pico-play" // the launch script runs this after the tool quits
#define DOWNLOAD_TMP	USERDATA_PATH "/pico-download.tmp"
#define MAX_CARTS		(BBS_PAGE_SIZE * 4)
#define MAX_PAGES		3 // pages of 30 fetched for one search

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
static BbsCart dl_cart;			// the cart being downloaded
static char dl_path[256];		// its file on the BBS
static uint32_t dl_drawn_at = 0;

// modal
static ModalKind modal = MODAL_NONE;
static char modal_msg[320];
static uint32_t modal_until = 0;
static int confirm_again = 0;	// the confirm is "download it again?" for dl_cart

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
static int is_saved(const BbsCart* c) {
	char dir[MAX_PATH], name[256], path[MAX_PATH+256];
	dest_dir(dir, sizeof(dir));
	bbs_file_name(c->title, NULL, name, sizeof(name));
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	return exists(path);
}

///////////////////////////////
// playing a cart that was just downloaded (the same way the Romulus tool does it)

static char play_cmd[MAX_PATH*2+64];
static char play_rom[MAX_PATH];
static int play_native = 0; // the emulator does not record play time itself
static void shell_quote(const char* in, char* out, size_t size) { // 'it'\''s'
	size_t n = 0;
	out[n++] = '\'';
	for (; *in && n+5<size; in++) {
		if (*in=='\'') { memcpy(out+n, "'\\''", 4); n += 4; }
		else out[n++] = *in;
	}
	out[n++] = '\'';
	out[n] = '\0';
}
static int make_play_cmd(const char* rom_path) { // 1 if the folder has an emulator on the card
	char emu_name[MAX_PATH], emu_path[MAX_PATH];
	getEmuName(rom_path, emu_name);
	getEmuPath(emu_name, emu_path);
	if (!emu_path[0] || !exists(emu_path)) return 0;
	char a[MAX_PATH*2], b[MAX_PATH*2];
	shell_quote(emu_path, a, sizeof(a));
	shell_quote(rom_path, b, sizeof(b));
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

#define WIFI_TIMEOUT_MS 30000
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
	if (!cmd || !*cmd) cmd = "wifictl";
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
	sel = scroll = 0;
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
	if (err) { page_failed = 1; exhausted = 1; if (cart_count==0) say(err, 0); return; }
	size_t len = 0;
	char* body = slurp(list_tmp, &len);
	BbsCart* got = malloc(sizeof(BbsCart) * BBS_PAGE_SIZE * 2);
	int n = (body && got) ? bbs_parse_search(body, len, got, BBS_PAGE_SIZE * 2) : -1;
	free(body);
	if (n<0) { free(got); page_failed = 1; exhausted = 1; if (cart_count==0) say("The BBS page made no sense", 0); return; }
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
	pages_loaded++;
	if (n<BBS_PAGE_SIZE || pages_loaded>=MAX_PAGES || cart_count>=MAX_CARTS) exhausted = 1;
	if (!list_is_search && browse_kind==BBS_BROWSE_RANDOM) exhausted = 1; // a shuffle is one page; Y for another
}

static void start_cart_download(void) { // the thread page said where the cart is
	char url[512];
	bbs_cart_url(url, sizeof(url), dl_path);
	if (bbs_job_start(&job, url, DOWNLOAD_TMP, 1)) { say("Couldn't start curl", 0); return; }
	job_kind = JOB_CART;
	PWR_disableAutosleep();
	dirty = 1;
}
static void finish_thread(void) {
	const char* err = bbs_job_error(&job);
	if (err) { say(err, 0); return; }
	size_t len = 0;
	char* body = slurp(list_tmp, &len);
	int ok = body && bbs_parse_cart_path(body, len, dl_path, sizeof(dl_path))==0;
	free(body);
	if (!ok) { say("That post has no cart file", 0); return; }
	start_cart_download();
}
static int copy_file(const char* from, const char* to) {
	FILE* in = fopen(from, "rb");
	if (!in) return -1;
	FILE* out = fopen(to, "wb");
	if (!out) { fclose(in); return -1; }
	char buf[16384];
	size_t n;
	int bad = 0;
	while ((n = fread(buf, 1, sizeof(buf), in))>0) if (fwrite(buf, 1, n, out)!=n) { bad = 1; break; }
	fclose(in);
	if (fclose(out)) bad = 1;
	return bad ? -1 : 0;
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
		if (copy_file(DOWNLOAD_TMP, part) || rename(part, path)) { remove(part); unlink(DOWNLOAD_TMP); say("Couldn't save the file (card full?)", 0); return; }
		unlink(DOWNLOAD_TMP);
	}
	sync();
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
	dl_cart = *c;
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
static void draw_row(int j, int selected, const char* text, const char* right, int dim) { // j counts from 0 below the title row
	int y = SCALE1(PADDING) + (j+1)*ROW_H;
	int w = screen->w - SCALE1(PADDING*2);
	SDL_Color color = dim ? COLOR_DARK_TEXT : COLOR_WHITE;
	int right_w = 0;
	SDL_Surface* rs = NULL;
	if (right && *right) {
		rs = GFX_renderText(font.small, right, selected ? COLOR_BLACK : COLOR_DARK_TEXT);
		if (rs) right_w = rs->w + SCALE1(BUTTON_PADDING);
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
		SDL_BlitSurface(rs, NULL, screen, &(SDL_Rect){SCALE1(PADDING)+w-rs->w-SCALE1(BUTTON_PADDING), y+(ROW_H-rs->h)/2});
		SDL_FreeSurface(rs);
	}
}
static void draw_note(const char* text) {
	GFX_blitMessage(font.large, (char*)text, screen, &(SDL_Rect){0, ROW_H, screen->w, screen->h-ROW_H*2});
}
static int cart_rows(void) { return list_rows() - 1; } // the cart list keeps its last row for the selected cart's details
static void keep_visible(int* s, int* top, int count, int rows) {
	if (*s<0) *s = 0;
	if (*s>=count) *s = count>0 ? count-1 : 0;
	if (*s<*top) *top = *s;
	if (*s>=*top+rows) *top = *s-rows+1;
	if (*top<0) *top = 0;
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
	else {
		int rows = cart_rows();
		int extra = (!exhausted && !page_failed) ? 1 : 0; // a last row while more is loading
		for (int j=0; j<rows && scroll+j<cart_count+extra; j++) {
			int i = scroll+j;
			if (i>=cart_count) { draw_row(j, 0, "Loading more...", NULL, 1); continue; }
			BbsCart* c = &carts[i];
			char right[80];
			int saved = is_saved(c);
			if (saved) snprintf(right, sizeof(right), "SAVED");
			else snprintf(right, sizeof(right), "*%d", c->stars);
			draw_row(j, i==sel, c->title, right, saved && i!=sel);
		}
		if (sel>=0 && sel<cart_count) { // under the list: the selected cart's tags, then who made it and when
			BbsCart* c = &carts[sel];
			int w = screen->w - SCALE1(PADDING*2) - SCALE1(BUTTON_PADDING*2);
			int y = SCALE1(PADDING) + (rows+1)*ROW_H + SCALE1(6);
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
			snprintf(line, sizeof(line), "by %s  -  %.10s  -  %d stars", c->author, c->date, c->stars);
			GFX_truncateText(font.small, line, out, w, 0);
			blit_text(font.small, out, COLOR_GRAY, SCALE1(PADDING+BUTTON_PADDING), y + SCALE1(15), w);
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
	if (PAD_justPressed(BTN_A)) { modal = MODAL_NONE; if (confirm_again) start_download(&dl_cart); dirty = 1; }
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
			static const BbsBrowse kinds[] = { BBS_BROWSE_FEATURED, BBS_BROWSE_FEATURED, BBS_BROWSE_POPULAR, BBS_BROWSE_NEWEST, BBS_BROWSE_RANDOM };
			if (home_sel==0) { osk_x = osk_y = 0; cur_screen = SCR_OSK; }
			else if (home_sel==HOME_ROWS-1) cur_screen = SCR_TAGS;
			else { list_is_search = 0; browse_kind = kinds[home_sel]; browse_tag[0] = '\0'; list_back = SCR_HOME; begin_load(); }
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
		snprintf(osk_text, sizeof(osk_text), "%s", query);
		osk_x = osk_y = 0;
		cur_screen = SCR_OSK;
		dirty = 1;
		return;
	}
	if (PAD_justPressed(BTN_Y) && !list_is_search && browse_kind==BBS_BROWSE_RANDOM) { stop_job(); begin_load(); dirty = 1; return; } // another shuffle
	if (PAD_justPressed(BTN_Y) && cart_count>1) {
		if (list_is_search) sort_mode = sort_mode==BBS_SORT_BEST ? BBS_SORT_STARS : sort_mode==BBS_SORT_STARS ? BBS_SORT_NEWEST : BBS_SORT_BEST;
		else sort_mode = sort_mode==BBS_SORT_LISTED ? BBS_SORT_STARS : sort_mode==BBS_SORT_STARS ? BBS_SORT_NEWEST : BBS_SORT_LISTED;
		bbs_sort(carts, cart_count, query, sort_mode);
		sel = scroll = 0; // the top of the new order is the point of changing it
		dirty = 1;
	}
	int extra = (!exhausted && !page_failed) ? 1 : 0;
	int count = cart_count + extra;
	if (count>0) {
		if (PAD_justRepeated(BTN_DPAD_UP)) { moved(&sel, count, -1); dirty = 1; }
		else if (PAD_justRepeated(BTN_DPAD_DOWN)) { moved(&sel, count, 1); dirty = 1; }
		else if (PAD_justRepeated(BTN_L1)) { moved(&sel, count, -cart_rows()); dirty = 1; }
		else if (PAD_justRepeated(BTN_R1)) { moved(&sel, count, cart_rows()); dirty = 1; }
		keep_visible(&sel, &scroll, count, cart_rows());
	}
	if (PAD_justPressed(BTN_A)) {
		if (cart_count==0 || sel>=cart_count) {
			if (job_kind==JOB_NONE && (page_failed || cart_count==0) && net_state==NET_READY) { page_failed = 0; if (pages_loaded==0) new_search(); else load_next_page(); }
		}
		else if (is_saved(&carts[sel])) {
			dl_cart = carts[sel];
			snprintf(modal_msg, sizeof(modal_msg), "You already have this one.\nDownload it again?");
			modal = MODAL_CONFIRM;
			confirm_again = 1;
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
			if (kind==JOB_PAGE) finish_page();
			else if (kind==JOB_THREAD) finish_thread();
			else if (kind==JOB_CART) finish_cart();
			bbs_job_cleanup(&job);
			dirty = 1;
		}
		// the rest of the search is fetched in the background while the list is being read
		if (job_kind==JOB_NONE && modal==MODAL_NONE && cur_screen==SCR_LIST && !exhausted && !page_failed && pages_loaded>0 && cart_count>0 && net_state==NET_READY) load_next_page();
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
	unlink(list_tmp);
	TAGen_close();
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
