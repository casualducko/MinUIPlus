// The System Info page, shared by Tools > Info and the launcher's Fn (MENU) page: battery, Wi-Fi,
// storage, memory, play time, CPU, uptime, the builds. The numbers update by themselves every few seconds.
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
#include "info_view.h"
#include "wifi_core.h"

#ifndef BUILD_HASH
#define BUILD_HASH "unknown"
#endif
#ifndef BUILD_DATE
#define BUILD_DATE ""
#endif

#define MAX_ROWS 20
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
static pid_t power_pid = 0; // wifictl, while it turns the radio on or off
static int power_on = 0;
static uint32_t power_at = 0;
static void gatherWifi(void) {
	char ssid[WIFI_SSID_MAX] = "";
	int signal = 0;
	wifi_line[0] = '\0';
	ip_line[0] = '\0';
	if (power_pid) { snprintf(wifi_line, sizeof(wifi_line), power_on ? "Turning on..." : "Turning off..."); snprintf(ip_line, sizeof(ip_line), "none"); return; }
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

// version.txt is "release\ncommit\n". Cuts text after the release line and points commit at the next line.
static void splitVersion(char* text, char** commit) {
	*commit = text + strlen(text); // an empty string
	char* nl = strchr(text, '\n');
	if (nl) {
		*nl = '\0';
		*commit = nl + 1;
		char* end = strchr(*commit, '\n');
		if (end) *end = '\0';
		trimTrailingNewlines(*commit);
	}
	trimTrailingNewlines(text);
}
void InfoView_gather(void) {
	row_count = 0;
	char text[4096], a[48], b[48];

	{
		char fork[64] = "";
		readText(SYSTEM_PATH "/fork-version.txt", fork, sizeof(fork));
		for (char* p=fork; *p; p++) if (*p=='\n' || *p=='\r') { *p = '\0'; break; }
		add("MinUIPlus", "%s", fork[0] ? fork : "not recorded");
	}

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
	{
		char release[MAX_PATH];
		getFile(ROOT_SYSTEM_PATH "/version.txt", release, MAX_PATH);
		char* commit;
		splitVersion(release, &commit); // a missing or short version.txt doesn't crash
		add("MinUI", "%s", release);
		add("Commit", "%s", commit);
	}
	add("Build", "%s %s", BUILD_DATE, BUILD_HASH);
	struct utsname un;
	if (uname(&un)==0) add("Kernel", "%s", un.release);
	add("Platform", "%s (Moss)", PLATFORM);
	add("Model", "%s", PLAT_getModel());
}

static void text(SDL_Surface* screen, TTF_Font* f, const char* s, SDL_Color color, int x, int y, int max_w, int right_align) {
	if (!s || !*s) return;
	SDL_Surface* t = GFX_renderText(f, s, color);
	if (!t) return;
	int w = MIN(t->w, max_w);
	SDL_BlitSurface(t, &(SDL_Rect){0,0,w,t->h}, screen, &(SDL_Rect){right_align ? x-w : x, y});
	SDL_FreeSurface(t);
}


int InfoView_rowCount(void) { return row_count; }
int InfoView_visibleRows(SDL_Surface* screen) {
	int row_h = SCALE1(PILL_SIZE);
	int top = SCALE1(PADDING) + row_h;
	int bottom = screen->h - SCALE1(PADDING+PILL_SIZE) - SCALE1(8);
	int visible = (bottom - top) / row_h;
	return visible<1 ? 1 : visible;
}
void InfoView_forceWifi(void) { wifi_at = 0; }

// the title, the rows from `first`, the scroll arrows. The button groups are the caller's.
void InfoView_draw(SDL_Surface* screen, int first, int ow) {
	int row_h = SCALE1(PILL_SIZE);
	int top = SCALE1(PADDING) + row_h;
	int bottom = screen->h - SCALE1(PADDING+PILL_SIZE) - SCALE1(8);
	int visible = InfoView_visibleRows(screen);
	int w = screen->w - SCALE1(PADDING*2);
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
}

// X: switch the radio on or off. wifictl takes a few seconds, so it runs on its own and InfoView_poll() watches it.
void InfoView_toggleWifi(void) {
	if (power_pid) return;
	power_on = !wifi_radio_on();
	char* enable[] = { "wifiup", NULL }; // wifictl enable, then the other saved networks if that one is out of range
	char* disable[] = { "wifictl", "disable", NULL };
	power_pid = wifi_spawn(power_on ? enable : disable);
	power_at = SDL_GetTicks();
	wifi_at = 0;
	InfoView_gather();
}
int InfoView_poll(void) { // 1 when the Wi-Fi row changed and the screen needs drawing again
	if (!power_pid) return 0;
	if (wifi_reap(power_pid, NULL) || SDL_GetTicks() - power_at > 60000) {
		power_pid = 0;
		wifi_at = 0;
		InfoView_gather();
		return 1;
	}
	return 0;
}
const char* InfoView_wifiHint(void) { return power_pid ? "WI-FI..." : wifi_radio_on() ? "WI-FI OFF" : "WI-FI ON"; } // what X will do (a switch in progress: it is still working)
