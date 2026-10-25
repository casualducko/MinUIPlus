// Romulus: browse a romulus server and download ROMs straight into the right Roms folder.
// See ROMULUS_HANDOFF.md for the server side. Settings live in USERDATA_PATH/romulus.cfg.
//
//   platforms  ->  games (A downloads, X searches)  ->  on-screen keyboard
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/statvfs.h>

#include <msettings.h>

#include "sdl.h"
#include "defines.h"
#include "api.h"
#include "utils.h"
#include "rapi.h"
#include "zipx.h"

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#define CFG_PATH		USERDATA_PATH "/romulus.cfg"
#define LEDGER_PATH		USERDATA_PATH "/romulus-downloaded.txt"
#define ROMULUS_LAST_PATH		USERDATA_PATH "/romulus-last.txt"
#define PLAY_NEXT_PATH	"/tmp/romulus-play" // the launch script runs this after Romulus quits
#define DOWNLOAD_TMP	USERDATA_PATH "/romulus-download.tmp"
#define PAGE_LIMIT		50
#define MAX_PLATS		40
#define PRELOAD_ROWS	6 // fetch the next page when the cursor is this close to the end

typedef enum { SCR_PLATFORMS, SCR_GAMES, SCR_OSK } Screen;
typedef enum { JOB_NONE, JOB_PLATFORMS, JOB_ROMS, JOB_DOWNLOAD } JobKind;
typedef enum { MODAL_NONE, MODAL_MESSAGE, MODAL_CONFIRM, MODAL_PLAY } ModalKind;

typedef struct { char slug[48]; int count; char label[96]; } PlatRow;

static SDL_Surface* screen;
static RConfig cfg;
static int dirty = 1;
static int quit = 0;

// platforms
static PlatRow plats[MAX_PLATS+1]; // [0] is "All" when there is more than one
static int plat_count = 0;
static int plat_sel = 0, plat_scroll = 0;
static int plats_failed = 0;
static char usable_csv[512];

// games
static char cur_slug[48];		// "" = all
static char cur_label[96];
static char query[64];
static RRomList roms;
static long roms_total = 0;
static int roms_offset = 0;		// entries the server has sent so far (what to ask for next)
static int roms_exhausted = 0;
static int roms_failed = 0;
static int sel = 0, scroll = 0;

// on-screen keyboard
static const char* osk_rows[4] = { "abcdefghij", "klmnopqrst", "uvwxyz0123", "456789.-'&" };
static int osk_x = 0, osk_y = 0;
static char osk_text[64];

static Screen cur_screen = SCR_PLATFORMS;

// background work
static RJob job;
static JobKind job_kind = JOB_NONE;
static char list_tmp[512];
static int dl_index = -1;		// which game is downloading
static uint32_t dl_drawn_at = 0;

// modal
static ModalKind modal = MODAL_NONE;
static char modal_msg[320];
static uint32_t modal_until = 0;
static int confirm_index = -1;

static int show_setting = 0;

///////////////////////////////

static int has_emu(const char* tag, void* ctx) {
	(void)ctx;
	char pak_path[MAX_PATH];
	snprintf(pak_path, sizeof(pak_path), "%s/Emus/%s.pak/launch.sh", PAKS_PATH, tag);
	if (exists(pak_path)) return 1;
	snprintf(pak_path, sizeof(pak_path), "%s/Emus/%s/%s.pak/launch.sh", SDCARD_PATH, PLATFORM, tag);
	return exists(pak_path);
}
static int dest_dir(const char* slug, int create, char* dir, size_t cap) {
	// the answer doesn't change while we run, and the list asks for every visible row on every redraw
	static struct { char slug[48]; char dir[MAX_PATH]; } cache[16];
	static int cached = 0;
	if (!create) {
		for (int i=0; i<cached; i++) if (!strcmp(cache[i].slug, slug)) { snprintf(dir, cap, "%s", cache[i].dir); return 0; }
	}
	int r = rom_dest_dir(&cfg, slug, sys_for_slug(slug), ROMS_PATH, has_emu, NULL, create, dir, cap);
	if (r==0 && cached<16 && strlen(slug)<sizeof(cache[0].slug) && strlen(dir)<sizeof(cache[0].dir)) {
		int known = 0;
		for (int i=0; i<cached; i++) if (!strcmp(cache[i].slug, slug)) known = 1;
		if (!known) { snprintf(cache[cached].slug, sizeof(cache[0].slug), "%s", slug); snprintf(cache[cached].dir, sizeof(cache[0].dir), "%s", dir); cached++; }
	}
	return r;
}
static int is_saved(const RRom* rom) {
	if (ledger_has(LEDGER_PATH, rom->id)) return 1;
	char dir[MAX_PATH], safe[256], path[MAX_PATH+260];
	if (dest_dir(rom->platform, 0, dir, sizeof(dir))) return 0;
	rom_safe_name(rom->file_name, sys_for_slug(rom->platform), safe, sizeof(safe));
	snprintf(path, sizeof(path), "%s/%s", dir, safe);
	return exists(path);
}

// A game just downloaded can be played from the confirmation: the same command the launcher
// would run ('emulator launch.sh' 'rom'), written for Romulus's launch.sh to run after we quit.
static char play_cmd[MAX_PATH*2+64];
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
static int make_play_cmd(const char* rom_path) { // 1 if this system has an emulator on the card
	char emu_name[MAX_PATH], emu_path[MAX_PATH];
	getEmuName(rom_path, emu_name);
	getEmuPath(emu_name, emu_path);
	if (!emu_path[0] || !exists(emu_path)) return 0;
	char a[MAX_PATH*2], b[MAX_PATH*2];
	shell_quote(emu_path, a, sizeof(a));
	shell_quote(rom_path, b, sizeof(b));
	snprintf(play_cmd, sizeof(play_cmd), "%s %s\n", a, b);
	return 1;
}
static void start_play(void) {
	putFile(PLAY_NEXT_PATH, play_cmd);
	putInt(RESUME_SLOT_PATH, 8); // a new game: no auto-resume of an old state
	quit = 1;
}

static void say(const char* msg, int ms) { // ms==0: until a button is pressed
	snprintf(modal_msg, sizeof(modal_msg), "%s", msg);
	modal = MODAL_MESSAGE;
	modal_until = ms ? SDL_GetTicks()+ms : 0;
	dirty = 1;
}
static void ask(const char* msg, int index) {
	snprintf(modal_msg, sizeof(modal_msg), "%s", msg);
	modal = MODAL_CONFIRM;
	confirm_index = index;
	dirty = 1;
}

static void stop_job(void) {
	if (job_kind!=JOB_NONE) {
		rjob_cancel(&job);
		rjob_cleanup(&job);
		if (job_kind==JOB_DOWNLOAD) unlink(DOWNLOAD_TMP);
		job_kind = JOB_NONE;
		PWR_enableAutosleep();
	}
}

///////////////////////////////
// jobs

// The network. On the device Wi-Fi may be off (wlan0 down or not there at all), and "link up"
// is not yet "has an address": check first, switch it on if it isn't, then wait for both.
// The paths and the command can be overridden for tests (ROMULUS_NET_DIR, ROMULUS_ROUTE_FILE, ROMULUS_WIFICTL).
#define WIFI_TIMEOUT_MS 30000
#define NET_RETRIES 3 // quiet retries of the first request after we switched Wi-Fi on (DNS is often last)
typedef enum { NET_READY, NET_ENABLING, NET_WAITING } NetState;
static NetState net_state = NET_READY;
static uint32_t net_deadline = 0;
static uint32_t net_drawn_at = 0;
static int net_retries = 0;
static uint32_t retry_at = 0;
static pid_t wifictl_pid = 0;

static const char* net_dir(void) { const char* e = getenv("ROMULUS_NET_DIR"); return (e && *e) ? e : "/sys/class/net"; }
static int net_managed(void) { return access(net_dir(), F_OK)==0; } // no /sys/class/net: not the device, nothing to manage
static int wifi_link_up(void) {
	char path[256], state[32] = "";
	snprintf(path, sizeof(path), "%s/wlan0/operstate", net_dir());
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	if (!fgets(state, sizeof(state), f)) state[0] = '\0';
	fclose(f);
	return !strncmp(state, "up", 2);
}
static int has_default_route(void) { // /proc/net/route: a line whose destination is 00000000
	const char* e = getenv("ROMULUS_ROUTE_FILE");
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
static int network_ready(void) { return has_default_route(); } // by whatever interface: Wi-Fi, a USB tether, Ethernet
static void start_wifictl(void) { // `wifictl enable` like the Wi-Fi tool does, in the background
	const char* cmd = getenv("ROMULUS_WIFICTL");
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

static void load_platforms(void) {
	char path[64];
	rapi_platforms_path(path, sizeof(path));
	plats_failed = 0;
	if (rjob_start(&job, &cfg, path, list_tmp, 0)) { plats_failed = 1; say("Couldn't start curl", 0); return; }
	job_kind = JOB_PLATFORMS;
	dirty = 1;
}
static void load_page(void) {
	char path[1024];
	rapi_roms_path(path, sizeof(path), cur_slug[0] ? cur_slug : NULL, usable_csv, query, roms_offset, PAGE_LIMIT);
	if (rjob_start(&job, &cfg, path, list_tmp, 0)) { roms_failed = 1; say("Couldn't start curl", 0); return; }
	job_kind = JOB_ROMS;
	roms_failed = 0;
	dirty = 1;
}
static void new_search(void) { // restart the list for the current platform and query
	stop_job();
	rromlist_free(&roms);
	roms_total = 0;
	roms_offset = 0;
	roms_exhausted = 0;
	roms_failed = 0;
	sel = scroll = 0;
	load_page();
}

static void save_last(void) {
	FILE* f = fopen(ROMULUS_LAST_PATH, "w");
	if (!f) return;
	fputs(cur_slug, f);
	fclose(f);
}
static void restore_last(void) {
	char last[64] = "";
	FILE* f = fopen(ROMULUS_LAST_PATH, "r");
	if (!f) return;
	if (fgets(last, sizeof(last), f)) last[strcspn(last, "\r\n")] = '\0';
	fclose(f);
	for (int i=0; i<plat_count; i++) {
		if (!strcmp(plats[i].slug, last)) { plat_sel = i; return; }
	}
}

static void finish_platforms(void) {
	const char* err = rjob_error(&job);
	if (err && net_retries>0 && (job.curl_exit==6 || job.curl_exit==7 || job.curl_exit==28)) {
		// we only just switched Wi-Fi on: the address may be there but not DNS yet, ask again quietly
		net_retries--;
		retry_at = SDL_GetTicks() + 2000;
		return;
	}
	if (err) { plats_failed = 1; say(err, 0); return; }
	size_t cap = 256*1024;
	FILE* f = fopen(list_tmp, "rb");
	if (!f) { plats_failed = 1; say("Couldn't read the answer", 0); return; }
	char* body = malloc(cap);
	size_t n = body ? fread(body, 1, cap-1, f) : 0;
	fclose(f);
	RPlat found[64];
	int count = body ? rapi_parse_platforms(body, n, found, 64) : -1;
	free(body);
	if (count<0) { plats_failed = 1; say("The server's platform list made no sense", 0); return; }

	plat_count = 0;
	usable_csv[0] = '\0';
	int total = 0;
	for (int i=0; i<count && plat_count<MAX_PLATS; i++) {
		char dir[MAX_PATH];
		if (dest_dir(found[i].slug, 0, dir, sizeof(dir))) { LOG_info("romulus: no emulator or folder for '%s', not listed\n", found[i].slug); continue; }
		const SysDef* sys = sys_for_slug(found[i].slug);
		PlatRow* row = &plats[plat_count+1]; // leave [0] for All
		snprintf(row->slug, sizeof(row->slug), "%s", found[i].slug);
		row->count = found[i].count;
		snprintf(row->label, sizeof(row->label), "%s (%d)", sys ? sys->name : found[i].slug, found[i].count);
		if (usable_csv[0] && strlen(usable_csv)+strlen(row->slug)+2<sizeof(usable_csv)) strcat(usable_csv, ",");
		if (strlen(usable_csv)+strlen(row->slug)+1<sizeof(usable_csv)) strcat(usable_csv, row->slug);
		total += found[i].count;
		plat_count++;
	}
	if (plat_count==0) { plats_failed = 1; say("The server has nothing this device can play", 0); return; }
	// shift so there is an All row first when it helps
	if (plat_count>1) {
		snprintf(plats[0].label, sizeof(plats[0].label), "All (%d)", total);
		plats[0].slug[0] = '\0';
		plats[0].count = total;
		plat_count += 1;
	}
	else {
		memmove(&plats[0], &plats[1], sizeof(PlatRow)); // one platform: no All row
	}
	plat_sel = plat_scroll = 0;
	restore_last();
	dirty = 1;
}

static void finish_page(void) {
	const char* err = rjob_error(&job);
	if (err) { roms_failed = 1; say(err, 0); return; }
	FILE* f = fopen(list_tmp, "rb");
	if (!f) { roms_failed = 1; say("Couldn't read the answer", 0); return; }
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len<0 || len>8*1024*1024) { fclose(f); roms_failed = 1; say("The answer was too big", 0); return; }
	char* body = malloc(len+1);
	size_t n = body ? fread(body, 1, len, f) : 0;
	fclose(f);
	int raw = 0;
	long total = roms_total;
	int added = body ? rapi_parse_roms(body, n, &roms, &total, &raw) : -1;
	free(body);
	if (added<0) { roms_failed = 1; say("The server's game list made no sense", 0); return; }
	roms_total = total;
	// The server leaves disc sets out after it has cut the page, so a page can come back short
	// while more remain: page by the size asked for, not by what came back (no repeats, no gaps).
	(void)raw;
	roms_offset += PAGE_LIMIT;
	if (roms_offset>=roms_total) roms_exhausted = 1;
	dirty = 1;
}

static int copy_file(const char* from, const char* to) {
	FILE* in = fopen(from, "rb");
	if (!in) return -1;
	FILE* out = fopen(to, "wb");
	if (!out) { fclose(in); return -1; }
	static char buf[64*1024];
	size_t n;
	int ok = 1;
	while ((n = fread(buf, 1, sizeof(buf), in))>0) {
		if (fwrite(buf, 1, n, out)!=n) { ok = 0; break; }
	}
	if (ferror(in)) ok = 0; // a read error is not the end of the file
	fclose(in);
	if (fclose(out)!=0) ok = 0;
	if (!ok) remove(to);
	return ok ? 0 : -1;
}

static void finish_download(void) {
	RRom rom = roms.items[dl_index];
	const char* err = rjob_error(&job);
	PWR_enableAutosleep();
	if (err) { unlink(DOWNLOAD_TMP); say(err, 0); return; }

	const SysDef* sys = sys_for_slug(rom.platform);
	char dir[MAX_PATH];
	if (dest_dir(rom.platform, 1, dir, sizeof(dir))) { unlink(DOWNLOAD_TMP); say("Couldn't make the ROM folder", 0); return; }

	char safe[256];
	rom_safe_name(rom.file_name, sys, safe, sizeof(safe));

	// a zip is unpacked: the first file the emulator takes
	int is_zip = 0;
	const char* dot = strrchr(safe, '.');
	if (dot && !strcasecmp(dot+1, "zip")) is_zip = 1;
	else {
		FILE* f = fopen(DOWNLOAD_TMP, "rb");
		unsigned char magic[4] = {0};
		if (f) { if (fread(magic, 1, 4, f)==4 && !memcmp(magic, "PK\3\4", 4)) is_zip = 1; fclose(f); }
	}

	char final_name[256];
	char final_path[MAX_PATH+260];
	char part_path[MAX_PATH+270];
	int result = 0;
	if (is_zip && sys) {
		ZipEntry entry;
		int r = zipx_find(DOWNLOAD_TMP, sys->exts, &entry);
		if (r==ZIPX_E_NONE) { unlink(DOWNLOAD_TMP); say("There's no game for this system in that zip", 0); return; }
		if (r!=ZIPX_OK) { unlink(DOWNLOAD_TMP); say("Couldn't open that zip", 0); return; }
		rom_safe_name(entry.name, sys, final_name, sizeof(final_name));
		snprintf(final_path, sizeof(final_path), "%s/%s", dir, final_name);
		snprintf(part_path, sizeof(part_path), "%s/.%s.part", dir, final_name);
		struct statvfs vfs;
		if (statvfs(dir, &vfs)==0 && (unsigned long long)vfs.f_bavail*vfs.f_frsize < (unsigned long long)entry.usize+1024*1024) { unlink(DOWNLOAD_TMP); say("Not enough room on the card", 0); return; }
		r = zipx_extract(DOWNLOAD_TMP, &entry, part_path);
		unlink(DOWNLOAD_TMP);
		if (r!=ZIPX_OK) { remove(part_path); say(r==ZIPX_E_WRITE ? "Couldn't write to the card (full?)" : "The zip is damaged", 0); return; }
		result = rename(part_path, final_path);
		if (result) { remove(part_path); say("Couldn't save the file", 0); return; }
	}
	else {
		snprintf(final_name, sizeof(final_name), "%s", safe);
		snprintf(final_path, sizeof(final_path), "%s/%s", dir, final_name);
		if (rename(DOWNLOAD_TMP, final_path)) {
			// not the same card/folder: copy
			snprintf(part_path, sizeof(part_path), "%s/.%s.part", dir, final_name);
			if (copy_file(DOWNLOAD_TMP, part_path) || rename(part_path, final_path)) { remove(part_path); unlink(DOWNLOAD_TMP); say("Couldn't save the file (card full?)", 0); return; }
			unlink(DOWNLOAD_TMP);
		}
	}
	sync(); // a ROM the user can see is a ROM that is on the card, even if the power goes now
	ledger_add(LEDGER_PATH, rom.id);

	const char* folder = strrchr(dir, '/');
	char msg[400];
	char title[256], place[128];
	snprintf(title, sizeof(title), "%s", final_name);
	char* tdot = strrchr(title, '.');
	if (tdot && strlen(tdot)<=5) *tdot = '\0'; // no extension
	snprintf(place, sizeof(place), "%s", folder ? folder+1 : dir);
	char* tag = strstr(place, " (");
	if (tag) *tag = '\0'; // "Super Nintendo (SFC)" -> "Super Nintendo"
	GFX_wrapText(font.large, title, screen->w - SCALE1(PADDING*2) - SCALE1(BUTTON_PADDING*2), 2);
	snprintf(msg, sizeof(msg), "Downloaded\n\n%s\n\nSaved to %s", title, place);
	if (make_play_cmd(final_path)) {
		snprintf(modal_msg, sizeof(modal_msg), "%s", msg);
		modal = MODAL_PLAY;
		modal_until = 0;
		dirty = 1;
	}
	else say(msg, 3000);
}

static void start_download(int index) {
	RRom* rom = &roms.items[index];
	const SysDef* sys = sys_for_slug(rom->platform);
	char dir[MAX_PATH];
	if (dest_dir(rom->platform, 0, dir, sizeof(dir))) { say("This device can't play that system", 0); return; }

	// room for the file (twice if it's a zip, it's unpacked next to itself)
	struct statvfs vfs;
	const char* where = access(dir, F_OK)==0 ? dir : ROMS_PATH;
	unsigned long long need = (unsigned long long)MAX(rom->size, 0) * 2 + 4*1024*1024;
	if (statvfs(where, &vfs)==0 && (unsigned long long)vfs.f_bavail*vfs.f_frsize < need) { say("Not enough room on the card", 0); return; }
	(void)sys;

	char path[64];
	rapi_download_path(path, sizeof(path), rom->id);
	stop_job();
	if (rjob_start(&job, &cfg, path, DOWNLOAD_TMP, 1)) { say("Couldn't start curl", 0); return; }
	job_kind = JOB_DOWNLOAD;
	dl_index = index;
	PWR_disableAutosleep(); // a long download must not sleep the device
	dirty = 1;
}

///////////////////////////////
// drawing

#define ROW_H SCALE1(PILL_SIZE)

static int list_rows(void) { return MAIN_ROW_COUNT - 1; } // the first row is the title

static void blit_text(TTF_Font* ttf, const char* text, SDL_Color color, int x, int y, int max_w) {
	if (!text || !*text) return;
	SDL_Surface* s = TTF_RenderUTF8_Blended(ttf, text, color);
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
// j counts from 0 below the title row
static void draw_row(int j, int selected, const char* text, const char* right, int dim) {
	int y = SCALE1(PADDING) + (j+1)*ROW_H;
	int w = screen->w - SCALE1(PADDING*2);
	SDL_Color color = dim ? COLOR_DARK_TEXT : COLOR_WHITE;
	int right_w = 0;
	SDL_Surface* rs = NULL;
	if (right && *right) {
		rs = TTF_RenderUTF8_Blended(font.small, right, selected ? COLOR_BLACK : COLOR_DARK_TEXT);
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
static void keep_visible(int* s, int* top, int count) {
	int rows = list_rows();
	if (*s<0) *s = 0;
	if (*s>=count) *s = count>0 ? count-1 : 0;
	if (*s<*top) *top = *s;
	if (*s>=*top+rows) *top = *s-rows+1;
	if (*top<0) *top = 0;
}

static void draw_platforms(int ow) {
	draw_title("Romulus", ow);
	if (plat_count==0) {
		if (net_state==NET_ENABLING) draw_note("Turning on Wi-Fi...");
		else if (net_state==NET_WAITING) draw_note("Waiting for the network...");
		else if (retry_at) draw_note("Connecting to the server...");
		else if (job_kind==JOB_PLATFORMS) draw_note("Loading...");
		else draw_note(plats_failed ? "Couldn't load the server's list.\nPress A to try again." : "Loading...");
	}
	else {
		for (int j=0; j<list_rows() && plat_scroll+j<plat_count; j++) {
			int i = plat_scroll+j;
			draw_row(j, i==plat_sel, plats[i].label, NULL, 0);
		}
	}
	GFX_blitButtonGroup((char*[]){"B","QUIT",NULL}, 0, screen, 0);
	if (plat_count) GFX_blitButtonGroup((char*[]){"A","OPEN",NULL}, 1, screen, 1);
	else if (plats_failed) GFX_blitButtonGroup((char*[]){"A","RETRY",NULL}, 1, screen, 1);
}
static void draw_games(int ow) {
	char title[256];
	if (query[0]) snprintf(title, sizeof(title), "%s: \"%s\"", cur_label, query);
	else snprintf(title, sizeof(title), "%s", cur_label);
	draw_title(title, ow);

	if (roms.count==0) {
		if (job_kind==JOB_ROMS) draw_note("Loading...");
		else if (roms_failed) draw_note("Couldn't load the list.\nPress A to try again.");
		else draw_note("No games found.");
	}
	else {
		int rows = list_rows();
		int extra = (roms_failed || (!roms_exhausted && job_kind==JOB_ROMS)) ? 1 : 0; // a last row about the next page
		for (int j=0; j<rows && scroll+j<roms.count+extra; j++) {
			int i = scroll+j;
			if (i>=roms.count) {
				draw_row(j, i==sel, roms_failed ? "Couldn't load more. Press A to retry" : "Loading...", NULL, 1);
				continue;
			}
			RRom* r = &roms.items[i];
			char right[64] = "";
			int saved = is_saved(r);
			if (saved) snprintf(right, sizeof(right), "SAVED");
			else if (r->region[0]) snprintf(right, sizeof(right), "%s", r->region);
			draw_row(j, i==sel, r->name, right, saved && i!=sel);
		}
	}
	GFX_blitButtonGroup((char*[]){"FN","SEARCH",NULL}, 0, screen, 0);
	GFX_blitButtonGroup((char*[]){"B","BACK","A","DOWNLOAD",NULL}, 1, screen, 1);
}
static void draw_osk(void) {
	// the text so far
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
			SDL_Surface* s = TTF_RenderUTF8_Blended(font.large, ch, on ? COLOR_BLACK : COLOR_WHITE);
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
		SDL_Surface* s = TTF_RenderUTF8_Blended(font.medium, names[b], on ? COLOR_BLACK : COLOR_WHITE);
		if (s) { SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){x+2+(ww-s->w)/2, y+(ROW_H-s->h)/2}); SDL_FreeSurface(s); }
	}
	GFX_blitButtonGroup((char*[]){"START","SEARCH",NULL}, 0, screen, 0);
	GFX_blitButtonGroup((char*[]){"B","DELETE","A","TYPE",NULL}, 1, screen, 1);
}
static void draw_progress(void) {
	RRom* rom = &roms.items[dl_index];
	int w = screen->w - SCALE1(PADDING*2) - SCALE1(BUTTON_PADDING*2);
	int x = (screen->w-w)/2;
	int y = screen->h/2 - ROW_H*2;
	char out[256];
	GFX_truncateText(font.large, rom->name, out, w, 0);
	blit_text(font.large, out, COLOR_WHITE, x, y, w);
	long got = rjob_bytes(&job);
	char line[96];
	if (rom->size>0) snprintf(line, sizeof(line), "%.1f of %.1f MB", got/1048576.0, rom->size/1048576.0);
	else snprintf(line, sizeof(line), "%.1f MB", got/1048576.0);
	blit_text(font.small, line, COLOR_LIGHT_TEXT, x, y+ROW_H, w);
	int bar_y = y + ROW_H*2;
	GFX_blitPill(ASSET_BAR_BG_MENU, screen, &(SDL_Rect){x, bar_y, w, SCALE1(SETTINGS_SIZE)});
	if (rom->size>0 && got>0) {
		float p = (float)got / (float)rom->size;
		if (p>1) p = 1;
		GFX_blitPill(ASSET_BAR, screen, &(SDL_Rect){x, bar_y, MAX((int)(w*p), SCALE1(SETTINGS_SIZE)), SCALE1(SETTINGS_SIZE)});
	}
	GFX_blitButtonGroup((char*[]){"B","CANCEL",NULL}, 0, screen, 1);
}
static void draw_modal(void) {
	int w = screen->w - SCALE1(PADDING*2) - SCALE1(BUTTON_PADDING*2);
	int h = ROW_H*5;
	int x = (screen->w-w)/2, y = (screen->h-h)/2;
	char text[320];
	snprintf(text, sizeof(text), "%s", modal_msg);
	if (!strchr(text, '\n')) GFX_wrapText(font.large, text, w, 6); // long one-line messages, the rest already have their breaks
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
	if (*s<0) *s = (step==-1 && before==0) ? count-1 : 0; // up from the top wraps like the launcher
	if (*s>=count) *s = (step==1 && before==count-1) ? 0 : count-1;
	return *s!=before;
}

static void open_games(void) {
	PlatRow* p = &plats[plat_sel];
	snprintf(cur_slug, sizeof(cur_slug), "%s", p->slug);
	snprintf(cur_label, sizeof(cur_label), "%s", p->slug[0] ? p->label : "All");
	// the title shouldn't carry the count twice
	char* paren = strrchr(cur_label, '(');
	if (paren && paren>cur_label && paren[-1]==' ') paren[-1] = '\0';
	query[0] = '\0';
	save_last();
	cur_screen = SCR_GAMES;
	new_search();
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
		// trim, then search
		while (n && osk_text[n-1]==' ') osk_text[--n] = '\0';
		const char* t = osk_text;
		while (*t==' ') t++;
		snprintf(query, sizeof(query), "%s", t);
		cur_screen = SCR_GAMES;
		new_search();
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
	if (PAD_justPressed(BTN_A)) {
		modal = MODAL_NONE;
		if (confirm_index>=0 && confirm_index<roms.count) start_download(confirm_index);
		dirty = 1;
	}
	else if (PAD_justPressed(BTN_B)) { modal = MODAL_NONE; dirty = 1; }
}

static void handle_input(void) {
	if (job_kind==JOB_DOWNLOAD) { // only cancel
		if (PAD_justPressed(BTN_B)) { stop_job(); say("Cancelled", 1200); }
		return;
	}
	if (modal!=MODAL_NONE) { handle_modal_input(); return; }

	if (cur_screen==SCR_PLATFORMS) {
		if (PAD_justPressed(BTN_B)) { quit = 1; return; }
		if (plat_count==0) {
			if (PAD_justPressed(BTN_A) && job_kind==JOB_NONE) load_platforms();
			return;
		}
		if (PAD_justRepeated(BTN_DPAD_UP)) { moved(&plat_sel, plat_count, -1); dirty = 1; }
		else if (PAD_justRepeated(BTN_DPAD_DOWN)) { moved(&plat_sel, plat_count, 1); dirty = 1; }
		else if (PAD_justRepeated(BTN_L1)) { moved(&plat_sel, plat_count, -list_rows()); dirty = 1; }
		else if (PAD_justRepeated(BTN_R1)) { moved(&plat_sel, plat_count, list_rows()); dirty = 1; }
		keep_visible(&plat_sel, &plat_scroll, plat_count);
		if (PAD_justPressed(BTN_A)) open_games();
		return;
	}
	if (cur_screen==SCR_OSK) {
		if (PAD_justRepeated(BTN_DPAD_LEFT)) { osk_x = (osk_x+9)%10; dirty = 1; }
		else if (PAD_justRepeated(BTN_DPAD_RIGHT)) { osk_x = (osk_x+1)%10; dirty = 1; }
		else if (PAD_justRepeated(BTN_DPAD_UP)) { osk_y = (osk_y+4)%5; dirty = 1; }
		else if (PAD_justRepeated(BTN_DPAD_DOWN)) { osk_y = (osk_y+1)%5; dirty = 1; }
		if (PAD_justPressed(BTN_A)) { do_osk_press(); dirty = 1; }
		else if (PAD_justRepeated(BTN_B)) {
			size_t n = strlen(osk_text);
			if (n) { osk_text[n-1] = '\0'; dirty = 1; }
			else if (PAD_justPressed(BTN_B)) { cur_screen = SCR_GAMES; dirty = 1; } // nothing left to delete: leave
		}
		else if (PAD_justPressed(BTN_Y)) { osk_text[0] = '\0'; dirty = 1; }
		else if (PAD_justPressed(BTN_START)) { osk_y = 4; osk_x = 8; do_osk_press(); dirty = 1; }
		return;
	}

	// games
	if (PAD_justPressed(BTN_B)) { stop_job(); rromlist_free(&roms); cur_screen = SCR_PLATFORMS; dirty = 1; return; }
	if (PAD_justPressed(BTN_X) || PAD_justPressed(BTN_MENU)) { // MENU is the Fn button on this device
		snprintf(osk_text, sizeof(osk_text), "%s", query);
		osk_x = osk_y = 0;
		cur_screen = SCR_OSK;
		dirty = 1;
		return;
	}
	int extra = (roms_failed || (!roms_exhausted && job_kind==JOB_ROMS)) ? 1 : 0;
	int count = roms.count + extra;
	if (count>0) {
		if (PAD_justRepeated(BTN_DPAD_UP)) { moved(&sel, count, -1); dirty = 1; }
		else if (PAD_justRepeated(BTN_DPAD_DOWN)) { moved(&sel, count, 1); dirty = 1; }
		else if (PAD_justRepeated(BTN_L1)) { moved(&sel, count, -list_rows()); dirty = 1; }
		else if (PAD_justRepeated(BTN_R1)) { moved(&sel, count, list_rows()); dirty = 1; }
		keep_visible(&sel, &scroll, count);
	}
	if (PAD_justPressed(BTN_A)) {
		if (roms.count==0 || sel>=roms.count) {
			if (job_kind==JOB_NONE && (roms_failed || roms.count==0)) { roms_failed = 0; load_page(); }
		}
		else if (is_saved(&roms.items[sel])) ask("You already have this one.\nDownload it again?", sel);
		else start_download(sel);
		dirty = 1;
	}
	// the next page, a little before the end of this one
	if (job_kind==JOB_NONE && !roms_exhausted && !roms_failed && roms.count>0 && sel>=roms.count-PRELOAD_ROWS) load_page();
}

///////////////////////////////

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	PWR_setCPUSpeed(CPU_SPEED_MENU);

	screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();

	const char* tmp = getenv("TMPDIR");
	snprintf(list_tmp, sizeof(list_tmp), "%s/romulus-list-%d.json", (tmp && *tmp) ? tmp : "/tmp", (int)getpid());

	unlink(DOWNLOAD_TMP); // left over if the device lost power in the middle of a download
	
	int cfg_state = rcfg_load(CFG_PATH, &cfg);
	if (cfg_state==RCFG_MISSING) {
		rcfg_write_template(CFG_PATH);
		say("Set up your server first:\nedit .userdata/" PLATFORM "/romulus.cfg\non the card (url and token).", 0);
	}
	else if (cfg_state!=RCFG_OK) {
		char msg[320];
		snprintf(msg, sizeof(msg), "%s.\nEdit .userdata/" PLATFORM "/romulus.cfg\non the card.", rcfg_message(cfg_state));
		say(msg, 0);
	}
	else if (!net_managed() || network_ready()) load_platforms();
	else {
		const char* t = getenv("ROMULUS_WIFI_TIMEOUT_MS");
		net_deadline = SDL_GetTicks() + (t ? (uint32_t)atoi(t) : WIFI_TIMEOUT_MS);
		net_retries = NET_RETRIES;
		if (wifi_link_up()) net_state = NET_WAITING; // on, just no address yet: don't touch it
		else { net_state = NET_ENABLING; start_wifictl(); }
	}
	
	int config_problem = cfg_state!=RCFG_OK;

	while (!quit) {
		GFX_startFrame();
		uint32_t now = SDL_GetTicks();

		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		reap_wifictl();
		if (net_state!=NET_READY) {
			if (network_ready() || now>=net_deadline) { // up with an address, or we've waited long enough: the error will say what's wrong
				net_state = NET_READY;
				load_platforms();
			}
			else {
				if (net_state==NET_ENABLING && wifi_link_up()) { net_state = NET_WAITING; dirty = 1; }
				if (now-net_drawn_at>=1000) { net_drawn_at = now; dirty = 1; }
			}
		}
		if (retry_at && now>=retry_at) { retry_at = 0; load_platforms(); }
		
		if (job_kind!=JOB_NONE && rjob_poll(&job)) {
			JobKind kind = job_kind;
			job_kind = JOB_NONE;
			if (kind==JOB_PLATFORMS) finish_platforms();
			else if (kind==JOB_ROMS) finish_page();
			else if (kind==JOB_DOWNLOAD) finish_download();
			rjob_cleanup(&job);
			dirty = 1;
		}
		if (job_kind==JOB_DOWNLOAD && now-dl_drawn_at>=150) { dirty = 1; dl_drawn_at = now; } // progress

		if (config_problem) {
			if (modal==MODAL_NONE) quit = 1; // nothing to do without a server
			else handle_modal_input();
		}
		else handle_input();
		if (modal==MODAL_MESSAGE && modal_until && now>=modal_until) { modal = MODAL_NONE; dirty = 1; }

		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			// a download or a message has the whole screen to itself
			if (!config_problem && job_kind==JOB_DOWNLOAD && modal==MODAL_NONE) draw_progress();
			else if (modal!=MODAL_NONE) draw_modal();
			else if (cur_screen==SCR_PLATFORMS) draw_platforms(ow);
			else if (cur_screen==SCR_GAMES) draw_games(ow);
			else draw_osk();
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

	stop_job();
	unlink(list_tmp);
	rromlist_free(&roms);
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
