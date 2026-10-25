// System info: battery, Wi-Fi, storage, memory, play time, CPU, uptime, the build. A refreshes now,
// Up/Down scroll, L/R page, B quits. The numbers update by themselves every few seconds.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "info_core.h"
#include "wifi_core.h"

#ifndef BUILD_HASH
#define BUILD_HASH "unknown"
#endif
#ifndef BUILD_DATE
#define BUILD_DATE ""
#endif

#define MAX_ROWS 16
typedef struct { char label[32]; char value[160]; } InfoRow;
static InfoRow rows[MAX_ROWS];
static int row_count = 0;

static void add(const char* label, const char* fmt, ...) {
	if (row_count>=MAX_ROWS) return;
	InfoRow* r = &rows[row_count++];
	snprintf(r->label, sizeof(r->label), "%s", label);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(r->value, sizeof(r->value), fmt, ap);
	va_end(ap);
}
static char* readText(const char* path, char* buf, size_t cap) { // a small system file, "" if it isn't there
	buf[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return buf;
	size_t n = fread(buf, 1, cap-1, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

// the network is asked about less often than the rest: it starts programs
static char wifi_line[128] = "";
static char ip_line[64] = "";
static uint32_t wifi_at = 0;
static void gatherWifi(void) {
	char ssid[WIFI_SSID_MAX] = "";
	int signal = 0;
	wifi_line[0] = '\0';
	ip_line[0] = '\0';
	if (!wifi_radio_on()) { snprintf(wifi_line, sizeof(wifi_line), "Off"); snprintf(ip_line, sizeof(ip_line), "none"); return; }
	char text[2048];
	char* argv[] = { "iw", "dev", "wlan0", "link", NULL };
	if (wifi_run(argv, text, sizeof(text), 3000)==0 && wifi_parse_link(text, ssid, sizeof(ssid), &signal)) {
		snprintf(wifi_line, sizeof(wifi_line), "%s (%s)", ssid, wifi_bars_label(wifi_bars(signal)));
		wifi_ip_address(ip_line, sizeof(ip_line));
	}
	else snprintf(wifi_line, sizeof(wifi_line), "On, not connected");
	if (!ip_line[0]) snprintf(ip_line, sizeof(ip_line), "none");
}

static void gather(void) {
	row_count = 0;
	char text[4096], a[48], b[48];

	int percent = PLAT_getBatteryPercent();
	int charging = 0, charge = 0;
	PLAT_getBatteryStatus(&charging, &charge);
	if (percent>=0) add("Battery", "%i%%%s", percent, charging ? ", charging" : "");
	else add("Battery", "unknown%s", charging ? ", charging" : "");

	uint32_t now = SDL_GetTicks();
	if (!wifi_at || now - wifi_at > 4000) { gatherWifi(); wifi_at = now; }
	add("Wi-Fi", "%s", wifi_line);
	add("IP address", "%s", ip_line);

	struct statvfs vfs;
	if (statvfs(SDCARD_PATH, &vfs)==0) {
		Info_bytes((uint64_t)vfs.f_bavail * vfs.f_frsize, a, sizeof(a));
		Info_bytes((uint64_t)vfs.f_blocks * vfs.f_frsize, b, sizeof(b));
		add("Card", "%s free of %s", a, b);
	}
	else add("Card", "unknown");

	uint64_t total = 0, avail = 0;
	if (Info_meminfo(readText("/proc/meminfo", text, sizeof(text)), &total, &avail)) {
		Info_bytes(avail, a, sizeof(a));
		Info_bytes(total, b, sizeof(b));
		add("Memory", "%s free of %s", a, b);
	}
	else add("Memory", "unknown");

	// play time, from what the emulator keeps
	{
		PlayRecord* records = calloc(PLAYTIME_MAX, sizeof(PlayRecord));
		char* file = allocFile(PLAYTIME_PATH);
		int count = (records && file) ? Playtime_parse(file, records, PLAYTIME_MAX) : 0;
		long seconds = 0;
		for (int i=0; i<count; i++) seconds += records[i].seconds;
		if (count>0) {
			Playtime_duration(seconds, a, sizeof(a));
			add("Play time", "%s in %i game%s", a, count, count==1 ? "" : "s");
		}
		else add("Play time", "nothing played yet");
		free(records);
		free(file);
	}

	char freq[32], temp[32];
	Info_frequency(readText("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", text, sizeof(text)), freq, sizeof(freq));
	if (!freq[0]) Info_frequency(readText("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq", text, sizeof(text)), freq, sizeof(freq));
	Info_temperature(readText("/sys/class/thermal/thermal_zone0/temp", text, sizeof(text)), temp, sizeof(temp));
	if (freq[0] && temp[0]) add("CPU", "%s, %s", freq, temp);
	else if (freq[0] || temp[0]) add("CPU", "%s", freq[0] ? freq : temp);
	else add("CPU", "unknown");

	long up = 0;
	readText("/proc/uptime", text, sizeof(text));
	up = atol(text);
	Info_duration(up, a, sizeof(a));
	add("Uptime", "%s", a);

	time_t t = time(NULL);
	struct tm tmv;
	if (localtime_r(&t, &tmv)) {
		char when[48];
		strftime(when, sizeof(when), "%Y-%m-%d  %H:%M", &tmv);
		add("Clock", "%s", when);
	}
	add("Build", "%s %s", BUILD_DATE, BUILD_HASH);
	struct utsname un;
	if (uname(&un)==0) add("Kernel", "%s", un.release);
	add("Platform", "%s", PLATFORM);
}

static void text(SDL_Surface* screen, TTF_Font* f, const char* s, SDL_Color color, int x, int y, int max_w, int right_align) {
	if (!s || !*s) return;
	SDL_Surface* t = GFX_renderText(f, s, color);
	if (!t) return;
	int w = MIN(t->w, max_w);
	SDL_BlitSurface(t, &(SDL_Rect){0,0,w,t->h}, screen, &(SDL_Rect){right_align ? x-w : x, y});
	SDL_FreeSurface(t);
}

int main(int argc, char* argv[]) {
	(void)argc; (void)argv;
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	SDL_Surface* screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();
	gather();

	int row_h = SCALE1(PILL_SIZE);
	int top = SCALE1(PADDING) + row_h;
	int bottom = screen->h - SCALE1(PADDING+PILL_SIZE) - SCALE1(8);
	int visible = (bottom - top) / row_h;
	if (visible<1) visible = 1;
	int w = screen->w - SCALE1(PADDING*2);

	int quit = 0, dirty = 1, show_setting = 0, first = 0;
	uint32_t refreshed = SDL_GetTicks();
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (PAD_justPressed(BTN_B)) quit = 1;
		else if (PAD_justRepeated(BTN_UP) && first>0) { first--; dirty = 1; }
		else if (PAD_justRepeated(BTN_DOWN) && first+visible<row_count) { first++; dirty = 1; }
		else if (PAD_justRepeated(BTN_LEFT) && first>0) { first = MAX(0, first-visible); dirty = 1; }
		else if (PAD_justRepeated(BTN_RIGHT) && first+visible<row_count) { first = MIN(row_count-visible, first+visible); dirty = 1; }
		else if (PAD_justPressed(BTN_A)) { wifi_at = 0; gather(); refreshed = SDL_GetTicks(); dirty = 1; }
		if (SDL_GetTicks() - refreshed > 3000) { gather(); refreshed = SDL_GetTicks(); dirty = 1; }
		if (first+visible>row_count) first = MAX(0, row_count-visible);

		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			char out[96];
			GFX_truncateText(font.large, "System Info", out, screen->w - SCALE1(PADDING*2) - ow, SCALE1(BUTTON_PADDING*2));
			text(screen, font.large, out, COLOR_LIGHT_TEXT, SCALE1(PADDING+BUTTON_PADDING), SCALE1(PADDING)+4, screen->w, 0);
			for (int i=0; i<visible && first+i<row_count; i++) {
				InfoRow* r = &rows[first+i];
				int y = top + i*row_h;
				int ty = y + (row_h - TTF_FontHeight(font.large))/2;
				int label_w = 0;
				TTF_SizeUTF8(font.large, r->label, &label_w, NULL);
				text(screen, font.large, r->label, COLOR_WHITE, SCALE1(PADDING+BUTTON_PADDING), ty, w, 0);
				char value[160];
				int room = w - label_w - SCALE1(BUTTON_PADDING*3);
				GFX_truncateText(font.medium, r->value, value, room, 0);
				int vh = TTF_FontHeight(font.medium);
				text(screen, font.medium, value, COLOR_LIGHT_TEXT, SCALE1(PADDING) + w - SCALE1(BUTTON_PADDING), y + (row_h - vh)/2, w, 1);
			}
			if (row_count>visible) {
				int sx = (screen->w - SCALE1(24))/2;
				int soy = SCALE1((PILL_SIZE - 4) / 2);
				if (first>0) GFX_blitAsset(ASSET_SCROLL_UP, NULL, screen, &(SDL_Rect){sx, SCALE1(PADDING) + soy});
				if (first+visible<row_count) GFX_blitAsset(ASSET_SCROLL_DOWN, NULL, screen, &(SDL_Rect){sx, bottom + SCALE1(2)});
			}
			GFX_blitButtonGroup((char*[]){"B","BACK","A","REFRESH",NULL}, 1, screen, 1);
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	return EXIT_SUCCESS;
}
