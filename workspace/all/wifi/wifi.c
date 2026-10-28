// Tools > Wi-Fi: turn Wi-Fi on and off, see the networks around, connect (typing the password on a
// keyboard), and keep several networks to switch between. Moss only keeps one network at a time:
// `wifictl enable <name> <password>` makes it the one, so choosing a saved network does that.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "wifi_core.h"

#define SAVED_PATH USERDATA_PATH "/wifi-networks.txt"
#define OLD_WIFI_TXT "./wifi.txt" // what the old tool read: name on the first line, password on the second
#define CONNECT_WAIT_MS 20000 // how long after wifictl has finished a connection may take (WIFI_CONNECT_WAIT_MS overrides, for tests)
#define CONNECT_LIMIT_MS 60000 // and for the whole thing, wifictl included
#define STATUS_EVERY_MS 3000

typedef enum { SCR_MAIN, SCR_NETWORKS, SCR_SAVED, SCR_KEYBOARD } Screen;
typedef enum { OP_NONE, OP_SCAN, OP_CONNECT, OP_POWER } Op;
typedef enum { MODAL_NONE, MODAL_MESSAGE, MODAL_CONFIRM } ModalKind;

static SDL_Surface* screen;
static int quit = 0, dirty = 1, show_setting = 0;
static Screen cur_screen = SCR_MAIN;

// what we know
static WifiSaved saved[WIFI_MAX_NETS];
static int saved_count = 0;
static WifiNet nets[WIFI_MAX_NETS];
static int net_count = 0;
static int radio_on = 0;
static char cur_ssid[WIFI_SSID_MAX] = "";
static int cur_signal = -100;
static char cur_ip[48] = "";
static uint32_t status_at = 0;

// list cursors
static int sel_main = 0, sel_net = 0, top_net = 0, sel_saved = 0, top_saved = 0;

// something running in the background (a scan, a connection, switching Wi-Fi on or off)
static Op op = OP_NONE;
static char op_text[200] = "";
static int op_phase = 0;
static uint32_t op_started = 0, op_phase_at = 0;
static pid_t op_pid = 0;
static pid_t bg_pid = 0; // started and left running (going back to the previous network)
static char op_ssid[WIFI_SSID_MAX], op_pass[WIFI_PASS_MAX];
static char prev_ssid[WIFI_SSID_MAX], prev_pass[WIFI_PASS_MAX];
static int op_power_on = 0;
static int pick_mode = 0, pick_done = 0, exit_code = EXIT_SUCCESS; // --pick: a tool's launcher asks which saved network to use; done = connected
static int saved_pick = 0; // the saved list was opened from the Wi-Fi row: it is the choice of network to connect to

// a message or a question over everything
static ModalKind modal = MODAL_NONE;
static char modal_msg[320];
static uint32_t modal_until = 0;
static int confirm_index = -1; // what a "yes" applies to

// the keyboard
typedef enum { KB_PASSWORD, KB_NAME } KbPurpose;
static KbPurpose kb_purpose = KB_PASSWORD;
static char kb_ssid[WIFI_SSID_MAX] = "";
static char kb_text[WIFI_PASS_MAX] = "";
static int kb_x = 0, kb_y = 1, kb_mode = 0, kb_reveal = 0;
static const char* kb_pages[3][4] = {
	{ "1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm.,_" },
	{ "1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM.,_" },
	{ "!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/?", "`~01234567" },
};

static void say(const char* msg, uint32_t ms) {
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

///////////////////////////////

static void trim_newline(char* s) {
	size_t n = strlen(s);
	while (n && (s[n-1]=='\n' || s[n-1]=='\r' || s[n-1]==' ')) s[--n] = '\0';
}
static void get_setting_value(const char* key, char* out, size_t cap) {
	char* argv[] = { "get_setting", (char*)key, NULL };
	if (wifi_run(argv, out, cap, 3000)!=0) { if (cap) out[0] = '\0'; return; }
	trim_newline(out);
}
static void set_setting_value(const char* key, const char* value) {
	char buf[64];
	char* argv[] = { "set_setting", (char*)key, (char*)value, NULL };
	wifi_run(argv, buf, sizeof(buf), 3000);
}

static void save_list(void) { wifi_saved_store(SAVED_PATH, saved, saved_count); }
static void load_saved(void) {
	saved_count = wifi_saved_load(SAVED_PATH, saved, WIFI_MAX_NETS);
	if (saved_count) return;
	// first time: bring in the network the old tool knew (wifi.txt), if any
	FILE* f = fopen(OLD_WIFI_TXT, "r");
	if (!f) return;
	char name[128] = "", pass[128] = "";
	if (fgets(name, sizeof(name), f)) trim_newline(name);
	if (fgets(pass, sizeof(pass), f)) trim_newline(pass);
	fclose(f);
	if (name[0] && strlen(name)<WIFI_SSID_MAX && strlen(pass)<WIFI_PASS_MAX) {
		saved_count = wifi_saved_put(saved, saved_count, WIFI_MAX_NETS, name, pass);
		save_list();
	}
}

static void refresh_status(void) {
	radio_on = wifi_radio_on();
	cur_ssid[0] = '\0';
	cur_ip[0] = '\0';
	cur_signal = -100;
	if (!radio_on) return;
	char text[1024];
	char* argv[] = { "iw", "dev", "wlan0", "link", NULL };
	if (wifi_run(argv, text, sizeof(text), 3000)==0) wifi_parse_link(text, cur_ssid, sizeof(cur_ssid), &cur_signal);
	if (cur_ssid[0]) wifi_ip_address(cur_ip, sizeof(cur_ip));
}

///////////////////////////////
// background work

static void op_finish(void) {
	op = OP_NONE;
	op_pid = 0;
	status_at = 0; // look again at once
	dirty = 1;
}
static void op_cancel(void) {
	if (op_pid>0) { wifi_kill(op_pid); int st; wifi_reap(op_pid, &st); }
	op_finish();
}

static void start_scan(void) {
	op = OP_SCAN;
	op_phase = 0;
	op_started = op_phase_at = SDL_GetTicks();
	snprintf(op_text, sizeof(op_text), "Looking for networks...");
	dirty = 1;
}
static void start_connect(const char* ssid, const char* pass) {
	op = OP_CONNECT;
	op_phase = 0;
	op_started = op_phase_at = SDL_GetTicks();
	snprintf(op_ssid, sizeof(op_ssid), "%s", ssid);
	snprintf(op_pass, sizeof(op_pass), "%s", pass);
	snprintf(op_text, sizeof(op_text), "Connecting to\n%s...", ssid);
	dirty = 1;
}
static void start_power(int on) {
	op = OP_POWER;
	op_phase = 0;
	op_power_on = on;
	op_started = op_phase_at = SDL_GetTicks();
	snprintf(op_text, sizeof(op_text), on ? "Turning Wi-Fi on..." : "Turning Wi-Fi off...");
	dirty = 1;
}

static void step_scan(uint32_t now) {
	if (op_phase==0) {
		if (!wifi_radio_on()) { // scanning needs the radio, without touching which network is set up
			char buf[64];
			char* unblock[] = { "rfkill", "unblock", "wifi", NULL };
			wifi_run(unblock, buf, sizeof(buf), 3000);
			char* enable[] = { "connmanctl", "enable", "wifi", NULL };
			wifi_run(enable, buf, sizeof(buf), 5000);
		}
		char* scan[] = { "connmanctl", "scan", "wifi", NULL };
		op_pid = wifi_spawn(scan);
		op_phase = 1; op_phase_at = now;
	}
	else if (op_phase==1) {
		int st;
		if (wifi_reap(op_pid, &st) || now-op_phase_at>10000) {
			if (now-op_phase_at>10000) wifi_kill(op_pid);
			op_pid = 0;
			op_phase = 2; op_phase_at = now;
		}
	}
	else if (op_phase==2) {
		if (now-op_phase_at<1500) return; // let the results come in
		static char text[65536]; // a busy neighbourhood fills many KB
		char* dump[] = { "iw", "dev", "wlan0", "scan", "dump", NULL };
		net_count = 0;
		if (wifi_run(dump, text, sizeof(text), 6000)==0) net_count = wifi_parse_scan(text, nets, WIFI_MAX_NETS);
		if (!net_count) { // iw shows nothing: the names connman knows
			char* services[] = { "connmanctl", "services", NULL };
			if (wifi_run(services, text, sizeof(text), 4000)==0) net_count = wifi_parse_services(text, nets, WIFI_MAX_NETS);
		}
		sel_net = top_net = 0;
		cur_screen = SCR_NETWORKS;
		refresh_status();
		op_finish();
		if (!net_count) say("No networks found.\nMove closer to a router\nor try again.", 0);
	}
}

static void step_connect(uint32_t now) {
	if (op_phase==0) {
		get_setting_value("wifi.ssid", prev_ssid, sizeof(prev_ssid));
		get_setting_value("wifi.key", prev_pass, sizeof(prev_pass));
		set_setting_value("wifi.ssid", op_ssid);
		set_setting_value("wifi.key", op_pass);
		char* with_pass[] = { "wifictl", "enable", op_ssid, op_pass, NULL };
		char* open[] = { "wifictl", "enable", op_ssid, NULL };
		op_pid = wifi_spawn(op_pass[0] ? with_pass : open);
		op_phase = 1; op_phase_at = now;
		return;
	}
	if (op_phase==1 && wifi_reap(op_pid, NULL)) { op_pid = 0; op_phase = 2; op_phase_at = now; }
	const char* wait_env = getenv("WIFI_CONNECT_WAIT_MS");
	uint32_t wait_ms = wait_env ? (uint32_t)atoi(wait_env) : CONNECT_WAIT_MS;
	if (now-op_started>=CONNECT_LIMIT_MS || (op_phase==2 && now-op_phase_at>=wait_ms)) {
		// give up: go back to the network that was set up before, so Wi-Fi isn't left half changed
		if (op_pid>0) { wifi_kill(op_pid); op_pid = 0; }
		char msg[320];
		snprintf(msg, sizeof(msg), "Couldn't connect to\n%s.\nCheck the password.", op_ssid);
		// the settings go back to what they were whatever happened (a wrong password typed for the very network
		// that was already set up must not leave the wrong one stored), and the old network is switched on again
		set_setting_value("wifi.ssid", prev_ssid);
		set_setting_value("wifi.key", prev_pass);
		if (prev_ssid[0]) {
			char* back_with[] = { "wifictl", "enable", prev_ssid, prev_pass, NULL };
			char* back_open[] = { "wifictl", "enable", prev_ssid, NULL };
			bg_pid = wifi_spawn(prev_pass[0] ? back_with : back_open);
			snprintf(msg+strlen(msg), sizeof(msg)-strlen(msg), "\nGoing back to %s.", prev_ssid);
		}
		op_finish();
		say(msg, 0);
		return;
	}
	if (op_phase==2) { // the command is done: are we on it?
		static uint32_t last_check = 0;
		if (now-last_check<1000) return;
		last_check = now;
		char text[1024], ssid[WIFI_SSID_MAX] = "";
		int signal = 0;
		char* link[] = { "iw", "dev", "wlan0", "link", NULL };
		int linked = wifi_run(link, text, sizeof(text), 3000)==0 && wifi_parse_link(text, ssid, sizeof(ssid), &signal) && !strcmp(ssid, op_ssid);
		if (linked && wifi_has_route()) {
			saved_count = wifi_saved_put(saved, saved_count, WIFI_MAX_NETS, op_ssid, op_pass);
			save_list();
			char ip[48] = "";
			wifi_ip_address(ip, sizeof(ip));
			char msg[320];
			snprintf(msg, sizeof(msg), "Connected to\n%s\n%s", op_ssid, ip);
			op_finish();
			say(msg, 3500);
			pick_done = 1;
		}
	}
}

// Wi-Fi on: wifictl brings up the one network that is set up (A on the Wi-Fi row with two or more saved networks
// shows the list instead: the person picks, nothing is tried in turn).
#define POWER_FIRST_WAIT_MS 12000 // the set-up network gets this long

static void step_power(uint32_t now) {
	if (op_phase==0) {
		char* on[] = { "wifictl", "enable", NULL };
		char* off[] = { "wifictl", "disable", NULL };
		get_setting_value("wifi.ssid", prev_ssid, sizeof(prev_ssid));
		get_setting_value("wifi.key", prev_pass, sizeof(prev_pass));
		op_pid = wifi_spawn(op_power_on ? on : off);
		op_phase = 1; op_phase_at = now;
		return;
	}
	if (op_phase==1) {
		if (wifi_reap(op_pid, NULL)) { op_pid = 0; op_phase = 2; op_phase_at = now; }
		else if (now-op_started>40000) { wifi_kill(op_pid); op_pid = 0; op_phase = 2; op_phase_at = now; }
		return;
	}
	if (!op_power_on) { op_finish(); return; }
	static uint32_t last_check = 0;
	if (now-last_check>=1000) { // waiting for a route
		last_check = now;
		if (wifi_has_route()) { op_finish(); return; }
	}
	if (now-op_phase_at>POWER_FIRST_WAIT_MS) {
		op_finish();
		say("Wi-Fi is on, but it hasn't\nconnected. Pick a network\nfrom Saved networks\nor Networks nearby.", 0);
	}
}

static void op_step(void) {
	if (op==OP_NONE) return;
	uint32_t now = SDL_GetTicks();
	if (op==OP_SCAN) step_scan(now);
	else if (op==OP_CONNECT) step_connect(now);
	else if (op==OP_POWER) step_power(now);
	if (op!=OP_NONE) dirty = 1; // the dots
}

///////////////////////////////
// drawing

#define ROW_H SCALE1(PILL_SIZE)
#define LIST_ROWS ((screen->h - SCALE1((PADDING + PILL_SIZE) * 2)) / ROW_H)

static void blit_text(TTF_Font* ttf, const char* text, SDL_Color color, int x, int y) {
	if (!text || !*text) return;
	SDL_Surface* s = GFX_renderText(ttf, text, color);
	if (!s) return;
	SDL_BlitSurface(s, NULL, screen, &(SDL_Rect){x, y});
	SDL_FreeSurface(s);
}
static void draw_title(const char* text, int ow) {
	char out[256];
	int w = screen->w - SCALE1(PADDING*2) - ow;
	GFX_truncateText(font.large, text, out, w, SCALE1(BUTTON_PADDING*2));
	blit_text(font.large, out, COLOR_LIGHT_TEXT, SCALE1(PADDING+BUTTON_PADDING), SCALE1(PADDING)+4);
}
// the same rows as the options pages: a white pill behind the name when selected, a dark one behind the row
static int draw_row(int j, int selected, const char* name, const char* value, int name_max) {
	int mw = screen->w - SCALE1(PADDING*2);
	int ox = SCALE1(PADDING);
	int y = SCALE1(PADDING + PILL_SIZE) + j*ROW_H;
	char shown[256];
	int pill_w = GFX_truncateText(font.large, name, shown, name_max>0 ? name_max : mw, SCALE1(BUTTON_PADDING*2));
	if (selected) {
		if (value) GFX_blitPill(ASSET_DARK_GRAY_PILL, screen, &(SDL_Rect){ox, y, mw, ROW_H});
		GFX_blitPill(ASSET_WHITE_PILL, screen, &(SDL_Rect){ox, y, pill_w, ROW_H});
	}
	blit_text(font.large, shown, selected ? COLOR_BLACK : COLOR_WHITE, ox+SCALE1(BUTTON_PADDING), y+4);
	if (value && *value) {
		char v[256];
		int room = mw - pill_w - SCALE1(BUTTON_PADDING*2);
		if (room>SCALE1(BUTTON_PADDING*2)) {
			GFX_truncateText(font.medium, value, v, room, 0);
			SDL_Surface* t = GFX_renderText(font.medium, v, COLOR_WHITE);
			if (t) {
				SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){ox + mw - t->w - SCALE1(BUTTON_PADDING), y + (ROW_H - t->h)/2});
				SDL_FreeSurface(t);
			}
		}
	}
	return y;
}
static void draw_note(const char* text) {
	GFX_blitMessage(font.large, (char*)text, screen, &(SDL_Rect){0, ROW_H, screen->w, screen->h-ROW_H*2});
}
static void keep_visible(int* s, int* top, int count) {
	int rows = LIST_ROWS;
	if (*s<0) *s = 0;
	if (*s>=count) *s = count>0 ? count-1 : 0;
	if (*s<*top) *top = *s;
	if (*s>=*top+rows) *top = *s-rows+1;
	if (*top<0) *top = 0;
}

// signal bars and a padlock, at the right end of a row
static void draw_signal(int y, int bars, int secured, int selected) {
	int right = screen->w - SCALE1(PADDING) - SCALE1(BUTTON_PADDING);
	(void)selected;
	int bw = SCALE1(3), gap = SCALE1(2), maxh = SCALE1(14);
	int x = right - 4*bw - 3*gap;
	for (int b=0; b<4; b++) {
		int h = maxh * (b+1) / 4;
		SDL_Rect r = { x + b*(bw+gap), y + (ROW_H + maxh)/2 - h, bw, h };
		Uint32 color = b<bars ? SDL_MapRGB(screen->format, 0xff, 0xff, 0xff) : SDL_MapRGB(screen->format, 0x55, 0x55, 0x55);
		SDL_FillRect(screen, &r, color);
	}
	if (secured) { // a small padlock
		int lw = SCALE1(8), lh = SCALE1(6);
		int lx = x - SCALE1(14) - lw, ly = y + ROW_H/2 - SCALE1(1);
		Uint32 white = SDL_MapRGB(screen->format, 0xff, 0xff, 0xff);
		SDL_FillRect(screen, &(SDL_Rect){lx, ly, lw, lh}, white);
		SDL_FillRect(screen, &(SDL_Rect){lx + SCALE1(2), ly - SCALE1(4), SCALE1(1), SCALE1(4)}, white);
		SDL_FillRect(screen, &(SDL_Rect){lx + lw - SCALE1(3), ly - SCALE1(4), SCALE1(1), SCALE1(4)}, white);
		SDL_FillRect(screen, &(SDL_Rect){lx + SCALE1(2), ly - SCALE1(4), lw - SCALE1(4), SCALE1(1)}, white);
	}
}

static void draw_main(int ow) {
	draw_title("Wi-Fi", ow);
	char value[96];
	if (!radio_on) snprintf(value, sizeof(value), "Off");
	else if (cur_ssid[0]) snprintf(value, sizeof(value), "On");
	else snprintf(value, sizeof(value), "On, not connected");
	draw_row(0, sel_main==0, "Wi-Fi", value, 0);
	draw_row(1, sel_main==1, "Networks nearby", "", 0);
	char count[24] = "";
	if (saved_count) snprintf(count, sizeof(count), "%i", saved_count);
	draw_row(2, sel_main==2, "Saved networks", count, 0);
	
	// what is going on
	char status[256];
	int y = SCALE1(PADDING + PILL_SIZE) + 4*ROW_H;
	if (!radio_on) snprintf(status, sizeof(status), "Wi-Fi is off.");
	else if (cur_ssid[0]) snprintf(status, sizeof(status), "Connected to %s", cur_ssid);
	else snprintf(status, sizeof(status), "Not connected to a network.");
	blit_text(font.medium, status, COLOR_WHITE, SCALE1(PADDING+BUTTON_PADDING), y);
	if (cur_ssid[0]) {
		char detail[160];
		snprintf(detail, sizeof(detail), "Signal: %s    Address: %s", wifi_bars_label(wifi_bars(cur_signal)), cur_ip[0] ? cur_ip : "waiting...");
		blit_text(font.medium, detail, COLOR_GRAY, SCALE1(PADDING+BUTTON_PADDING), y + SCALE1(20));
	}
	GFX_blitButtonGroup((char*[]){"B","BACK","A","SELECT",NULL}, 1, screen, 1);
}

static void draw_networks(int ow) {
	draw_title("Networks nearby", ow);
	int count = net_count + 1; // and "Other network..."
	keep_visible(&sel_net, &top_net, count);
	for (int i=top_net, j=0; i<count && j<LIST_ROWS; i++, j++) {
		if (i==net_count) { draw_row(j, i==sel_net, "Other network...", "", 0); continue; }
		WifiNet* n = &nets[i];
		int y = SCALE1(PADDING + PILL_SIZE) + j*ROW_H;
		const char* tag = "";
		if (cur_ssid[0] && !strcmp(cur_ssid, n->ssid)) tag = "Connected";
		else if (wifi_saved_find(saved, saved_count, n->ssid)>=0) tag = "Saved";
		int mw = screen->w - SCALE1(PADDING*2);
		draw_row(j, i==sel_net, n->ssid, "", mw * 55 / 100);
		if (*tag) {
			SDL_Surface* t = GFX_renderText(font.medium, tag, i==sel_net ? COLOR_WHITE : COLOR_GRAY);
			if (t) {
				SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){screen->w - SCALE1(PADDING + BUTTON_PADDING + 72) - t->w, y + (ROW_H - t->h)/2});
				SDL_FreeSurface(t);
			}
		}
		draw_signal(y, wifi_bars(n->signal), n->secured, i==sel_net);
	}
	GFX_blitButtonGroup((char*[]){"X","RESCAN",NULL}, 0, screen, 0);
	GFX_blitButtonGroup((char*[]){"B","BACK","A","CONNECT",NULL}, 1, screen, 1);
}

static void draw_saved(int ow) {
	draw_title(saved_pick ? "Choose a network" : "Saved networks", ow);
	if (!saved_count) {
		draw_note("Nothing saved yet.\nConnect to a network from\nNetworks nearby to keep it here.");
		GFX_blitButtonGroup((char*[]){"B","BACK",NULL}, 1, screen, 1);
		return;
	}
	keep_visible(&sel_saved, &top_saved, saved_count);
	for (int i=top_saved, j=0; i<saved_count && j<LIST_ROWS; i++, j++) {
		int connected = cur_ssid[0] && !strcmp(cur_ssid, saved[i].ssid);
		draw_row(j, i==sel_saved, saved[i].ssid, connected ? "Connected" : (saved[i].pass[0] ? "" : "Open"), 0);
	}
	GFX_blitButtonGroup((char*[]){"X","FORGET",NULL}, 0, screen, 0);
	GFX_blitButtonGroup((char*[]){"B","BACK","A","CONNECT",NULL}, 1, screen, 1);
}

static void draw_keyboard(int ow) {
	char title[160];
	if (kb_purpose==KB_NAME) snprintf(title, sizeof(title), "Network name");
	else snprintf(title, sizeof(title), "Password for %s", kb_ssid);
	draw_title(title, ow);
	
	// what has been typed
	char shown[WIFI_PASS_MAX + 2];
	if (kb_purpose==KB_NAME || kb_reveal) snprintf(shown, sizeof(shown), "%s", kb_text);
	else { size_t n = strlen(kb_text); memset(shown, '*', n); shown[n] = '\0'; }
	char line[WIFI_PASS_MAX + 4];
	snprintf(line, sizeof(line), "%s_", shown);
	int field_y = SCALE1(PADDING + PILL_SIZE);
	int field_w = screen->w - SCALE1(PADDING*2);
	GFX_blitPill(ASSET_DARK_GRAY_PILL, screen, &(SDL_Rect){SCALE1(PADDING), field_y, field_w, ROW_H});
	char fit[WIFI_PASS_MAX + 8];
	const char* tail = line;
	int w = 0;
	TTF_SizeUTF8(font.large, tail, &w, NULL);
	while (w > field_w - SCALE1(BUTTON_PADDING*2) && tail[1]) { tail++; TTF_SizeUTF8(font.large, tail, &w, NULL); } // keep the end in view
	snprintf(fit, sizeof(fit), "%s", tail);
	blit_text(font.large, fit, COLOR_WHITE, SCALE1(PADDING+BUTTON_PADDING), field_y + 4);
	char count[16];
	snprintf(count, sizeof(count), "%i/%i", (int)strlen(kb_text), WIFI_PASS_MAX-1);
	blit_text(font.small, count, COLOR_GRAY, screen->w - SCALE1(PADDING+BUTTON_PADDING) - 70, field_y + ROW_H + SCALE1(2));
	
	// keys
	int grid_x = SCALE1(PADDING);
	int cell_w = field_w / 10;
	int cell_h = ROW_H * 96 / 100;
	int grid_y = field_y + ROW_H + SCALE1(16);
	for (int r=0; r<4; r++) {
		for (int c=0; c<10; c++) {
			char ch[2] = { kb_pages[kb_mode][r][c], 0 };
			int on = (kb_y==r && kb_x==c);
			SDL_Rect cell = { grid_x + c*cell_w + SCALE1(2), grid_y + r*cell_h, cell_w - SCALE1(4), cell_h - SCALE1(4) };
			if (on) GFX_fillCapsule(screen, cell, SDL_MapRGB(screen->format, 0xff, 0xff, 0xff));
			SDL_Surface* t = GFX_renderText(font.large, ch[0]==' ' ? "_" : ch, on ? COLOR_BLACK : COLOR_WHITE);
			if (t) {
				SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){cell.x + (cell.w - t->w)/2, cell.y + (cell.h - t->h)/2});
				SDL_FreeSurface(t);
			}
		}
	}
	const char* mode_label[3] = { "abc", "ABC", "#+="};
	const char* actions[5] = { mode_label[kb_mode], "Space", "Delete", kb_reveal ? "Hide" : "Show", "Done" };
	int cur = kb_x / 2;
	for (int b=0; b<5; b++) {
		int on = (kb_y==4 && cur==b);
		SDL_Rect cell = { grid_x + b*2*cell_w + SCALE1(2), grid_y + 4*cell_h, 2*cell_w - SCALE1(4), cell_h - SCALE1(4) };
		if (on) GFX_fillCapsule(screen, cell, SDL_MapRGB(screen->format, 0xff, 0xff, 0xff));
		SDL_Surface* t = GFX_renderText(font.medium, actions[b], on ? COLOR_BLACK : COLOR_WHITE);
		if (t) {
			SDL_BlitSurface(t, NULL, screen, &(SDL_Rect){cell.x + (cell.w - t->w)/2, cell.y + (cell.h - t->h)/2});
			SDL_FreeSurface(t);
		}
	}
	GFX_blitButtonGroup((char*[]){"X","ABC","Y","SHOW",NULL}, 0, screen, 0);
	GFX_blitButtonGroup((char*[]){"B","DELETE","A","TYPE",NULL}, 1, screen, 1);
}

static void draw_busy(void) {
	// the message, and dots that move so it's clear it hasn't hung
	int dots = (SDL_GetTicks() / 400) % 4;
	char text[256];
	snprintf(text, sizeof(text), "%s", op_text);
	char* end = strstr(text, "...");
	if (end) end[dots] = '\0'; // 0 to 3 dots
	GFX_blitMessage(font.large, text, screen, &(SDL_Rect){0, 0, screen->w, screen->h - ROW_H});
	GFX_blitButtonGroup((char*[]){"B","CANCEL",NULL}, 0, screen, 1);
}
static void draw_modal(void) {
	int w = screen->w - SCALE1(PADDING*2) - SCALE1(BUTTON_PADDING*2);
	int h = ROW_H*4;
	char text[320];
	snprintf(text, sizeof(text), "%s", modal_msg);
	GFX_blitMessage(font.large, text, screen, &(SDL_Rect){(screen->w-w)/2, (screen->h-h)/2, w, h});
	if (modal==MODAL_CONFIRM) GFX_blitButtonGroup((char*[]){"B","NO","A","YES",NULL}, 1, screen, 1);
	else if (!modal_until) GFX_blitButtonGroup((char*[]){"A","OK",NULL}, 1, screen, 1);
}

///////////////////////////////
// input

static void go_connect(const char* ssid, const char* pass) {
	start_connect(ssid, pass);
}
static void open_keyboard(KbPurpose purpose, const char* ssid) {
	kb_purpose = purpose;
	snprintf(kb_ssid, sizeof(kb_ssid), "%s", ssid);
	kb_text[0] = '\0';
	kb_x = 0; kb_y = 1; kb_mode = 0; kb_reveal = 0;
	cur_screen = SCR_KEYBOARD;
	dirty = 1;
}
static void kb_append(char c) {
	size_t n = strlen(kb_text);
	size_t cap = kb_purpose==KB_NAME ? WIFI_SSID_MAX : WIFI_PASS_MAX;
	if (n+1<cap) { kb_text[n] = c; kb_text[n+1] = '\0'; }
}
static void kb_done(void) {
	if (kb_purpose==KB_NAME) {
		if (!kb_text[0]) return;
		char name[WIFI_SSID_MAX];
		snprintf(name, sizeof(name), "%s", kb_text);
		open_keyboard(KB_PASSWORD, name);
		return;
	}
	if (kb_text[0] && strlen(kb_text)<8) { say("A Wi-Fi password is at least\n8 characters.\n(Leave it empty for an open network.)", 0); return; }
	cur_screen = SCR_NETWORKS;
	go_connect(kb_ssid, kb_text);
}
static void kb_press(void) {
	if (kb_y<4) { kb_append(kb_pages[kb_mode][kb_y][kb_x]); return; }
	switch (kb_x/2) {
		case 0: kb_mode = (kb_mode+1) % 3; break;
		case 1: kb_append(' '); break;
		case 2: { size_t n = strlen(kb_text); if (n) kb_text[n-1] = '\0'; break; }
		case 3: kb_reveal = !kb_reveal; break;
		default: kb_done(); break;
	}
}

static void connect_to_net(int index) {
	WifiNet* n = &nets[index];
	int s = wifi_saved_find(saved, saved_count, n->ssid);
	if (s>=0) { go_connect(saved[s].ssid, saved[s].pass); return; }
	if (!n->secured) { go_connect(n->ssid, ""); return; }
	open_keyboard(KB_PASSWORD, n->ssid);
}

static void handle_modal_input(void) {
	if (modal==MODAL_MESSAGE) {
		if (modal_until && SDL_GetTicks()>=modal_until) { modal = MODAL_NONE; dirty = 1; return; }
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) { modal = MODAL_NONE; dirty = 1; }
		return;
	}
	if (PAD_justPressed(BTN_A)) {
		modal = MODAL_NONE;
		if (confirm_index>=0 && confirm_index<saved_count) {
			saved_count = wifi_saved_remove(saved, saved_count, confirm_index);
			save_list();
			if (sel_saved>=saved_count) sel_saved = saved_count>0 ? saved_count-1 : 0;
		}
		confirm_index = -1;
		dirty = 1;
	}
	else if (PAD_justPressed(BTN_B)) { modal = MODAL_NONE; confirm_index = -1; dirty = 1; }
}

static void handle_input(void) {
	if (modal!=MODAL_NONE) { handle_modal_input(); return; }
	if (op!=OP_NONE) {
		if (PAD_justPressed(BTN_B)) op_cancel();
		return;
	}
	if (cur_screen==SCR_KEYBOARD) {
		if (PAD_justRepeated(BTN_LEFT)) { kb_x = (kb_x+9)%10; dirty = 1; }
		else if (PAD_justRepeated(BTN_RIGHT)) { kb_x = (kb_x+1)%10; dirty = 1; }
		else if (PAD_justRepeated(BTN_UP)) { kb_y = (kb_y+4)%5; dirty = 1; }
		else if (PAD_justRepeated(BTN_DOWN)) { kb_y = (kb_y+1)%5; dirty = 1; }
		if (PAD_justPressed(BTN_A)) { kb_press(); dirty = 1; }
		else if (PAD_justPressed(BTN_B)) {
			size_t n = strlen(kb_text);
			if (n) kb_text[n-1] = '\0';
			else cur_screen = SCR_NETWORKS; // nothing to delete: back out
			dirty = 1;
		}
		else if (PAD_justPressed(BTN_X)) { kb_mode = (kb_mode+1) % 3; dirty = 1; }
		else if (PAD_justPressed(BTN_Y)) { kb_reveal = !kb_reveal; dirty = 1; }
		else if (PAD_justPressed(BTN_START)) { kb_done(); dirty = 1; }
		return;
	}
	
	int count = cur_screen==SCR_MAIN ? 3 : (cur_screen==SCR_NETWORKS ? net_count+1 : saved_count);
	int* sel = cur_screen==SCR_MAIN ? &sel_main : (cur_screen==SCR_NETWORKS ? &sel_net : &sel_saved);
	if (count>0) {
		if (PAD_justRepeated(BTN_UP)) { *sel = *sel>0 ? *sel-1 : count-1; dirty = 1; }
		else if (PAD_justRepeated(BTN_DOWN)) { *sel = *sel<count-1 ? *sel+1 : 0; dirty = 1; }
	}
	
	if (PAD_justPressed(BTN_B)) {
		if (cur_screen==SCR_MAIN) quit = 1;
		else if (pick_mode) { quit = 1; exit_code = 1; } // cancelled: the tool is not started
		else cur_screen = SCR_MAIN;
		dirty = 1;
	}
	else if (PAD_justPressed(BTN_A)) {
		if (cur_screen==SCR_MAIN) {
			if (sel_main==0) {
				if (radio_on) start_power(0);
				else {
					char ssid[WIFI_SSID_MAX];
					get_setting_value("wifi.ssid", ssid, sizeof(ssid));
					if (!ssid[0] && !saved_count) say("There's no network set up yet.\nChoose Networks nearby\nto connect to one.", 0);
					else if (saved_count>=2) { cur_screen = SCR_SAVED; sel_saved = top_saved = 0; saved_pick = 1; } // pick one, nothing is tried in turn
					else start_power(1);
				}
			}
			else if (sel_main==1) start_scan();
			else { cur_screen = SCR_SAVED; sel_saved = top_saved = 0; saved_pick = 0; }
		}
		else if (cur_screen==SCR_NETWORKS) {
			if (sel_net==net_count) open_keyboard(KB_NAME, "");
			else connect_to_net(sel_net);
		}
		else if (cur_screen==SCR_SAVED && saved_count) go_connect(saved[sel_saved].ssid, saved[sel_saved].pass);
		dirty = 1;
	}
	else if (PAD_justPressed(BTN_X)) {
		if (cur_screen==SCR_NETWORKS) start_scan();
		else if (cur_screen==SCR_SAVED && saved_count) {
			char msg[160];
			snprintf(msg, sizeof(msg), "Forget %s?", saved[sel_saved].ssid);
			ask(msg, sel_saved);
		}
	}
}

int main(int argc, char* argv[]) {
	for (int i=1; i<argc; i++) if (!strcmp(argv[i], "--pick")) pick_mode = 1;
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	
	screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();
	
	mkdirs(USERDATA_PATH);
	load_saved();
	refresh_status();
	status_at = SDL_GetTicks();
	
	// --pick: before a tool that needs the network. With two or more saved networks and Wi-Fi off, ask which one;
	// otherwise there is nothing to ask and the tool brings Wi-Fi up itself.
	if (pick_mode) {
		if (wifi_has_route() || radio_on || saved_count<2) quit = 1;
		else { cur_screen = SCR_SAVED; sel_saved = top_saved = 0; saved_pick = 1; dirty = 1; }
	}
	
	while (!quit) {
		GFX_startFrame();
		uint32_t now = SDL_GetTicks();
		
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);
		
		op_step();
		if (bg_pid>0 && wifi_reap(bg_pid, NULL)) bg_pid = 0;
		if (op==OP_NONE && modal==MODAL_NONE && now-status_at>=STATUS_EVERY_MS) {
			status_at = now;
			int was_on = radio_on;
			char was_ssid[WIFI_SSID_MAX];
			snprintf(was_ssid, sizeof(was_ssid), "%s", cur_ssid);
			refresh_status();
			if (was_on!=radio_on || strcmp(was_ssid, cur_ssid)) dirty = 1;
		}
		handle_input();
		if (modal==MODAL_MESSAGE && modal_until && now>=modal_until) { modal = MODAL_NONE; dirty = 1; }
		if (pick_mode && pick_done && modal==MODAL_NONE && op==OP_NONE) quit = 1;
		
		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			if (modal!=MODAL_NONE) draw_modal();
			else if (op!=OP_NONE) draw_busy();
			else if (cur_screen==SCR_MAIN) draw_main(ow);
			else if (cur_screen==SCR_NETWORKS) draw_networks(ow);
			else if (cur_screen==SCR_SAVED) draw_saved(ow);
			else draw_keyboard(ow);
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}
	
	if (op!=OP_NONE) op_cancel();
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return exit_code;
}
