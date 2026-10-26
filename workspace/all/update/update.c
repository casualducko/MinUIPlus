// Update: checks the project's GitHub releases for a newer build, downloads it, checks it and leaves it
// staged; Update.pak/launch.sh then runs the package's install.sh (which keeps a backup of everything it
// replaces) and calls this tool again with --done to say how it went.
//   A checks (and, when there is a newer build, installs it), B goes back.
// Settings: .userdata/<platform>/update.cfg (repo=owner/name, token=... for a private repo).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "clocknet.h"
#include "update_core.h"

#define WORK_DIR USERDATA_PATH "/update"
#define STAGE_DIR WORK_DIR "/stage"
#define PKG_FILE WORK_DIR "/package.tar.gz"
#define SUM_FILE WORK_DIR "/package.sha256"
#define VERSION_FILE SYSTEM_PATH "/fork-version.txt"
#define CONFIG_FILE USERDATA_PATH "/update.cfg"
#define RESULT_FILE WORK_DIR "/result.txt"
#define READY_FILE "/tmp/update-ready"
#define RELEASE_JSON "/tmp/update-release.json"
#define CURL_CONFIG "/tmp/update-curl.cfg"

static SDL_Surface* screen;
static char repo[128], token[256];
static char installed[64] = "";
static UpdateRelease release;
static char progress_path[MAX_PATH] = ""; // a file that is growing: shown as a size
static uint64_t progress_total = 0;

static void text(TTF_Font* f, const char* s, SDL_Color color, int x, int y, int max_w) {
	if (!s || !*s) return;
	SDL_Surface* t = TTF_RenderUTF8_Blended(f, s, color);
	if (!t) return;
	SDL_BlitSurface(t, &(SDL_Rect){0,0,MIN(t->w, max_w),t->h}, screen, &(SDL_Rect){x,y});
	SDL_FreeSurface(t);
}
static void megabytes(uint64_t bytes, char* out, size_t size) {
	if (bytes<1048576) snprintf(out, size, "%.0f KB", bytes / 1024.0);
	else snprintf(out, size, "%.1f MB", bytes / 1048576.0);
}
static void message(const char* msg, const char* hints[]) { // a centered message with a button group
	GFX_clearUI(screen);
	GFX_blitMessage(font.large, (char*)msg, screen, &(SDL_Rect){0, SCALE1(PADDING), screen->w, screen->h - SCALE1(PILL_SIZE+PADDING)});
	GFX_blitButtonGroup((char**)hints, 1, screen, 1);
	GFX_flip(screen);
}
static void waitForA(const char* msg) {
	message(msg, (const char*[]){"A","OKAY",NULL});
	while (1) {
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) break;
		GFX_sync();
	}
}
static int netTick(void* ctx, const char* msg) { // shows what is going on (and a size, while a file arrives); 1 if B was pressed
	(void)ctx;
	GFX_startFrame();
	PAD_poll();
	static uint32_t drawn = 0;
	static char last[160] = "";
	if (strcmp(last, msg) || SDL_GetTicks() - drawn > 700) {
		snprintf(last, sizeof(last), "%s", msg);
		char line[256];
		struct stat st;
		if (progress_path[0] && !stat(progress_path, &st)) {
			char a[32], b[32];
			megabytes(st.st_size, a, sizeof(a));
			if (progress_total) { megabytes(progress_total, b, sizeof(b)); snprintf(line, sizeof(line), "%s\n%s of %s", msg, a, b); }
			else snprintf(line, sizeof(line), "%s\n%s", msg, a);
		}
		else snprintf(line, sizeof(line), "%s", msg);
		message(line, (const char*[]){"B","CANCEL",NULL});
		drawn = SDL_GetTicks();
	}
	return PAD_justPressed(BTN_B);
}

static void readInstalled(void) {
	installed[0] = '\0';
	getFile(VERSION_FILE, installed, sizeof(installed));
	for (char* p = installed; *p; p++) if (*p=='\n' || *p=='\r') { *p = '\0'; break; }
}
static void readConfig(void) {
	char* cfg = allocFile(CONFIG_FILE);
	Update_parseConfig(cfg, repo, sizeof(repo), token, sizeof(token));
	free(cfg);
}

// Runs curl for url into file. *code gets the http status, 0 if none. Only https, also across redirects, unless a test
// server is named (UPDATE_API).
// Returns NULL, or a message for the user. The token never goes on a command line: a 0600 file holds it.
static const char* fetch(const char* url, const char* file, int timeout_ms, int* code, const char* accept) {
	*code = 0;
	char* argv[24];
	int a = 0;
	argv[a++] = "curl";
	argv[a++] = "-sS"; argv[a++] = "-L";
	argv[a++] = "--proto"; argv[a++] = getenv("UPDATE_API") ? "=http,https" : "=https";
	argv[a++] = "--proto-redir"; argv[a++] = getenv("UPDATE_API") ? "=http,https" : "=https";
	argv[a++] = "--connect-timeout"; argv[a++] = "10";
	argv[a++] = "-o"; argv[a++] = (char*)file;
	argv[a++] = "-w"; argv[a++] = "%{http_code}";
	argv[a++] = "-H"; argv[a++] = "User-Agent: MinUIPlus-updater";
	argv[a++] = "-H"; argv[a++] = (char*)accept;
	if (token[0]) {
		unlink(CURL_CONFIG);
		int fd = open(CURL_CONFIG, O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW, 0600);
		if (fd<0) return "Couldn't write a temporary file.";
		char line[400];
		int n = snprintf(line, sizeof(line), "header = \"Authorization: Bearer %s\"\n", token);
		ssize_t w = write(fd, line, n);
		close(fd);
		if (w!=n) { unlink(CURL_CONFIG); return "Couldn't write a temporary file."; }
		argv[a++] = "-K"; argv[a++] = CURL_CONFIG;
	}
	argv[a++] = (char*)url;
	argv[a] = NULL;
	char out[32];
	int r = ClockNet_capture(argv, out, sizeof(out), timeout_ms, netTick, NULL, "Contacting GitHub...");
	unlink(CURL_CONFIG);
	if (r==-2) return "Cancelled.";
	if (r==-3) return "That took too long.\nCheck the Wi-Fi connection.";
	if (r<0) return "curl isn't available on this device.";
	*code = atoi(out);
	if (!*code) return "Couldn't reach GitHub.\nCheck the Wi-Fi connection.";
	return NULL;
}
static const char* httpMessage(int code) {
	static char buf[96];
	if (code==404) return token[0] ? "Not found.\nIs the repo right, and can the token read it?" : "No release found.\nA private repo needs a token in update.cfg.";
	if (code==401) return "GitHub refused the token.";
	if (code==403) return "GitHub said no (a rate limit, or the token can't read it).";
	snprintf(buf, sizeof(buf), "GitHub answered %i.", code);
	return buf;
}
// runs a shell snippet that prints OK when it worked ($1, $2 are the arguments)
static int shellOK(const char* script, const char* a1, const char* a2, int timeout_ms, const char* msg) {
	char* argv[] = { "sh", "-c", (char*)script, "sh", (char*)a1, (char*)a2, NULL };
	char out[64];
	int r = ClockNet_capture(argv, out, sizeof(out), timeout_ms, netTick, NULL, msg);
	return r==0 && strstr(out, "OK")!=NULL;
}

static const char* check(void) { // asks for the latest release; NULL or a message
	const char* error = NULL;
	if (ClockNet_wifiUp(&error, netTick, NULL)) return error;
	const char* base = getenv("UPDATE_API");
	char url[300];
	snprintf(url, sizeof(url), "%s/repos/%s/releases/latest", (base && *base) ? base : "https://api.github.com", repo);
	progress_path[0] = '\0';
	int code = 0;
	unlink(RELEASE_JSON);
	const char* e = fetch(url, RELEASE_JSON, 40000, &code, "Accept: application/vnd.github+json");
	if (e) return e;
	if (code!=200) return httpMessage(code);
	char* json = allocFile(RELEASE_JSON);
	unlink(RELEASE_JSON);
	int ok = json && Update_parseRelease(json, &release);
	free(json);
	return ok ? NULL : "That release has no update package.";
}
// downloads, checks and unpacks the package; NULL when it is staged, or a message
static const char* prepare(void) {
	mkdir(USERDATA_PATH, 0777);
	mkdir(WORK_DIR, 0777);
	struct statvfs sv;
	if (release.pkg_size > (512ULL<<20)) return "That update is too big to be right."; // (and keeps the sum below from wrapping)
	if (!statvfs(WORK_DIR, &sv) && release.pkg_size && (uint64_t)sv.f_bavail * sv.f_frsize < release.pkg_size * 3 + (16ULL<<20)) return "Not enough room on the card.";
	if (!shellOK("rm -rf \"$1\" \"$2\" && echo OK", STAGE_DIR, PKG_FILE, 30000, "Clearing the last update...")) return "Couldn't clear the last update."; // (old files must not be mixed into this one)
	unlink(SUM_FILE);
	int code = 0;
	const char* e;

	progress_path[0] = '\0';
	if ((e = fetch(release.sum_url, SUM_FILE, 60000, &code, "Accept: application/octet-stream"))) return e;
	if (code!=200) return httpMessage(code);
	char* sums = allocFile(SUM_FILE);
	char want[65];
	int have = sums && Update_parseSum(sums, want);
	free(sums);
	if (!have) return "The checksum file is not valid.";

	snprintf(progress_path, sizeof(progress_path), "%s", PKG_FILE);
	progress_total = release.pkg_size;
	e = fetch(release.pkg_url, PKG_FILE, 900000, &code, "Accept: application/octet-stream");
	progress_path[0] = '\0';
	if (e) return e;
	if (code!=200) return httpMessage(code);

	char* argv[] = { "sh", "-c", "sha256sum \"$1\" 2>/dev/null || shasum -a 256 \"$1\"", "sh", PKG_FILE, NULL };
	char out[256];
	int sum_run = ClockNet_capture(argv, out, sizeof(out), 120000, netTick, NULL, "Checking the download...");
	if (sum_run==-2) return "Cancelled.";
	if (sum_run!=0) return "Couldn't check the download.";
	char got[65];
	if (!Update_parseSum(out, got) || strcmp(got, want)) { unlink(PKG_FILE); return "The download is damaged.\nNothing was changed."; }

	char* list = malloc(256*1024);
	if (!list) return "Out of memory.";
	char* largv[] = { "tar", "tzf", PKG_FILE, NULL };
	int r = ClockNet_capture(largv, list, 256*1024, 60000, netTick, NULL, "Looking inside...");
	int safe = r==0 && strlen(list)<256*1024-2 && Update_pathsSafe(list);
	if (safe) { // and no links (a link out of the folder, then a file written through it, would pass a check of the names)
		char* vargv[] = { "tar", "tvzf", PKG_FILE, NULL };
		r = ClockNet_capture(vargv, list, 256*1024, 60000, netTick, NULL, "Looking inside...");
		safe = r==0 && strlen(list)<256*1024-2 && Update_noLinks(list);
	}
	free(list);
	if (!safe) { unlink(PKG_FILE); return "The package is not valid.\nNothing was changed."; }

	if (!shellOK("mkdir -p \"$2\" && tar xzf \"$1\" -C \"$2\" && echo OK", PKG_FILE, STAGE_DIR, 300000, "Unpacking...")) return "Couldn't unpack the update.";
	if (!shellOK("cd \"$1\" && test -f install.sh && test -f VERSION && { sha256sum -c SHA256SUMS || shasum -a 256 -c SHA256SUMS; } >/dev/null 2>&1 && echo OK", STAGE_DIR, "", 120000, "Checking every file..."))
		return "The files inside are damaged.\nNothing was changed.";
	return NULL;
}

static int doneMain(void) { // after install.sh: say how it went; on success A restarts the device
	char result[32] = "";
	getFile(RESULT_FILE, result, sizeof(result));
	readInstalled();
	char msg[256];
	if (strncmp(result, "ok", 2)) {
		snprintf(msg, sizeof(msg), "The update stopped part way.\nYour old files are in the newest\nbackup-before-fork folder in .system/%s.", PLATFORM);
		waitForA(msg);
		return 0;
	}
	snprintf(msg, sizeof(msg), "Updated to %s.", installed[0] ? installed : "the new build");
	message(msg, (const char*[]){"A","REBOOT",NULL});
	while (1) { // only A leaves: the new build is not fully in use until the device restarts
		GFX_startFrame();
		PAD_poll();
		if (PAD_justPressed(BTN_A)) break;
		GFX_sync();
	}
	return 1;
}

enum { ST_HOME, ST_RESULT };

int main(int argc, char* argv[]) {
	PWR_setCPUSpeed(CPU_SPEED_MENU);
	screen = GFX_init(MODE_MAIN);
	PAD_init();
	PWR_init();
	InitSettings();
	readConfig();
	readInstalled();
	int reboot_now = 0;
	if (argc>1 && !strcmp(argv[1], "--done")) { reboot_now = doneMain(); goto out; }
	if (!Update_repoValid(repo)) { waitForA("update.cfg has a repo that is not\nlike owner/name."); goto out; }

	int quit = 0, dirty = 1, show_setting = 0, state = ST_HOME;
	int newer = 0;
	while (!quit) {
		GFX_startFrame();
		PAD_poll();
		PWR_update(&dirty, &show_setting, NULL, NULL);

		if (PAD_justPressed(BTN_B)) { if (state==ST_RESULT) { state = ST_HOME; dirty = 1; } else quit = 1; }
		else if (PAD_justPressed(BTN_A)) {
			if (state==ST_HOME) {
				const char* e = check();
				if (e) waitForA(e);
				else { newer = Update_compareVersions(installed, release.tag) < 0; state = ST_RESULT; }
				dirty = 1;
			}
			else if (state==ST_RESULT && newer) {
				PWR_disableAutosleep();
				const char* e = prepare();
				PWR_enableAutosleep();
				if (e) { waitForA(e); state = ST_HOME; }
				else { putFile(READY_FILE, STAGE_DIR "\n"); quit = 1; }
				dirty = 1;
			}
			else if (state==ST_RESULT) { state = ST_HOME; dirty = 1; }
		}

		if (dirty) {
			GFX_clearUI(screen);
			int ow = GFX_blitHardwareGroup(screen, show_setting);
			int tx = SCALE1(PADDING+BUTTON_PADDING);
			int w = screen->w - SCALE1(PADDING*2);
			int row_h = SCALE1(PILL_SIZE);
			char out[160], line[256];
			GFX_truncateText(font.large, "Update", out, screen->w - SCALE1(PADDING*2) - ow, SCALE1(BUTTON_PADDING*2));
			text(font.large, out, COLOR_LIGHT_TEXT, tx, SCALE1(PADDING)+4, screen->w);
			int y = SCALE1(PADDING) + row_h + SCALE1(8);
			snprintf(line, sizeof(line), "You have: %s", installed[0] ? installed : "not recorded yet");
			text(font.large, line, COLOR_WHITE, tx, y, w);
			if (state==ST_HOME) {
				GFX_truncateText(font.small, repo, out, w - SCALE1(BUTTON_PADDING*2), 0);
				snprintf(line, sizeof(line), "From github.com/%s", out);
				text(font.small, line, COLOR_GRAY, tx, y + row_h + SCALE1(4), w);
				text(font.small, token[0] ? "Private repo token: set" : "No token (public releases only)", COLOR_GRAY, tx, y + row_h + SCALE1(22), w);
				text(font.small, "Every file it replaces is backed up first.", COLOR_GRAY, tx, y + row_h + SCALE1(40), w);
				GFX_blitButtonGroup((char*[]){"B","BACK","A","CHECK",NULL}, 1, screen, 1);
			}
			else {
				snprintf(line, sizeof(line), "Latest: %s", release.tag);
				text(font.large, line, COLOR_WHITE, tx, y + row_h, w);
				if (newer) {
					char size[32];
					megabytes(release.pkg_size, size, sizeof(size));
					snprintf(line, sizeof(line), "A newer build is ready (%s).", release.pkg_size ? size : "size unknown");
					text(font.small, line, COLOR_LIGHT_TEXT, tx, y + row_h*2 + SCALE1(8), w);
					text(font.small, "Your saves, games and settings stay as they are.", COLOR_GRAY, tx, y + row_h*2 + SCALE1(26), w);
					GFX_blitButtonGroup((char*[]){"B","BACK","A","INSTALL",NULL}, 1, screen, 1);
				}
				else {
					text(font.small, "You are up to date.", COLOR_LIGHT_TEXT, tx, y + row_h*2 + SCALE1(8), w);
					GFX_blitButtonGroup((char*[]){"A","OKAY",NULL}, 1, screen, 1);
				}
			}
			GFX_flip(screen);
			dirty = 0;
		}
		else GFX_sync();
	}

out:
	QuitSettings();
	PWR_quit();
	PAD_quit();
	GFX_quit();
	if (reboot_now) {
		sync();
		execlp("reboot", "reboot", (char*)NULL);
	}
	return EXIT_SUCCESS;
}
