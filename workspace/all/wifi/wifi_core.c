#include "wifi_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>

static int hexval(int c) {
	if (c>='0' && c<='9') return c-'0';
	if (c>='a' && c<='f') return c-'a'+10;
	if (c>='A' && c<='F') return c-'A'+10;
	return -1;
}

int wifi_ssid_unescape(const char* in, char* out, size_t cap) {
	size_t n = 0;
	if (!cap) return 0;
	for (const char* p=in; *p && n+1<cap; ) {
		if (p[0]=='\\' && p[1]=='x' && hexval(p[2])>=0 && hexval(p[3])>=0) {
			out[n++] = (char)(hexval(p[2])*16 + hexval(p[3]));
			p += 4;
		}
		else out[n++] = *p++;
	}
	out[n] = '\0';
	return (int)n;
}

static void trim(char* s) {
	char* start = s;
	while (*start==' ' || *start=='\t') start++;
	size_t len = strlen(start);
	while (len && (start[len-1]=='\n' || start[len-1]=='\r' || start[len-1]==' ' || start[len-1]=='\t')) start[--len] = '\0';
	if (start!=s) memmove(s, start, len+1);
}

static int usable_ssid(const char* s) { // not empty and not all zero bytes escaped (a hidden network)
	if (!*s) return 0;
	for (const char* p=s; *p; p++) if (*p!='\0') return 1;
	return 0;
}

typedef struct { WifiNet net; int has_rsn; int has_privacy; int active; } Bss;

static void bss_finish(Bss* b, WifiNet* out, int* count, int max) {
	if (!b->active) return;
	b->active = 0;
	// a name of nothing but \x00 escapes is a hidden network
	const char* s = b->net.ssid;
	int hidden = !*s;
	if (!hidden) {
		hidden = 1;
		for (const char* p=s; *p; p++) if ((unsigned char)*p>=0x20) { hidden = 0; break; }
	}
	if (hidden) return;
	b->net.secured = b->has_rsn || b->has_privacy;
	for (int i=0; i<*count; i++) {
		if (!strcmp(out[i].ssid, b->net.ssid)) { // the same name on another access point: keep the strongest
			if (b->net.signal > out[i].signal) { out[i].signal = b->net.signal; out[i].secured = b->net.secured; }
			return;
		}
	}
	if (*count<max) out[(*count)++] = b->net;
}

int wifi_parse_scan(const char* text, WifiNet* out, int max) {
	int count = 0;
	Bss b;
	memset(&b, 0, sizeof(b));
	const char* p = text;
	while (p && *p) {
		const char* eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol-p) : strlen(p);
		char line[512];
		if (len>=sizeof(line)) len = sizeof(line)-1;
		memcpy(line, p, len);
		line[len] = '\0';
		p = eol ? eol+1 : NULL;
		
		if (!strncmp(line, "BSS ", 4)) { // a new access point
			bss_finish(&b, out, &count, max);
			memset(&b, 0, sizeof(b));
			b.active = 1;
			b.net.signal = -100;
			continue;
		}
		if (!b.active) continue;
		char* l = line;
		while (*l=='\t' || *l==' ') l++;
		if (!strncmp(l, "signal:", 7)) b.net.signal = (int)(strtod(l+7, NULL) + (strtod(l+7, NULL)<0 ? -0.5 : 0.5));
		else if (!strncmp(l, "SSID:", 5)) {
			char raw[512];
			snprintf(raw, sizeof(raw), "%s", l+5);
			trim(raw);
			char name[WIFI_SSID_MAX * 4];
			wifi_ssid_unescape(raw, name, sizeof(name));
			snprintf(b.net.ssid, sizeof(b.net.ssid), "%s", name);
		}
		else if (!strncmp(l, "RSN:", 4) || !strncmp(l, "WPA:", 4)) b.has_rsn = 1;
		else if (!strncmp(l, "capability:", 11) && strstr(l, "Privacy")) b.has_privacy = 1;
	}
	bss_finish(&b, out, &count, max);
	// strongest first
	for (int i=1; i<count; i++) {
		WifiNet t = out[i];
		int j = i-1;
		while (j>=0 && out[j].signal < t.signal) { out[j+1] = out[j]; j--; }
		out[j+1] = t;
	}
	return count;
}

int wifi_parse_services(const char* text, WifiNet* out, int max) {
	int count = 0;
	const char* p = text;
	while (p && *p && count<max) {
		const char* eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol-p) : strlen(p);
		char line[512];
		if (len>=sizeof(line)) len = sizeof(line)-1;
		memcpy(line, p, len);
		line[len] = '\0';
		p = eol ? eol+1 : NULL;
		char* id = strstr(line, "wifi_");
		if (!id) continue;
		// the id is the last word; the name is what's between the 4 character prefix and it
		char* last = id;
		for (char* q=id; *q; q++) if ((q==line || q[-1]==' ') && !strncmp(q, "wifi_", 5)) last = q;
		id = last;
		size_t prefix = len>=4 ? 4 : 0;
		char name[512];
		size_t n = (size_t)(id - line);
		if (n<=prefix) continue;
		snprintf(name, sizeof(name), "%.*s", (int)(n-prefix), line+prefix);
		trim(name);
		if (!name[0] || strlen(name)>=WIFI_SSID_MAX) continue; // hidden services have no name
		size_t idlen = strlen(id);
		int open = idlen>=5 && !strcmp(id+idlen-5, "_none");
		int dup = 0;
		for (int i=0; i<count; i++) if (!strcmp(out[i].ssid, name)) dup = 1;
		if (dup) continue;
		snprintf(out[count].ssid, sizeof(out[count].ssid), "%s", name);
		out[count].signal = -70;
		out[count].secured = !open;
		count++;
	}
	return count;
}

int wifi_parse_link(const char* text, char* ssid, size_t cap, int* signal) {
	if (cap) ssid[0] = '\0';
	if (signal) *signal = -100;
	if (!text || strstr(text, "Not connected")) return 0;
	int connected = 0;
	const char* p = text;
	while (p && *p) {
		const char* eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol-p) : strlen(p);
		char line[512];
		if (len>=sizeof(line)) len = sizeof(line)-1;
		memcpy(line, p, len);
		line[len] = '\0';
		p = eol ? eol+1 : NULL;
		char* l = line;
		while (*l=='\t' || *l==' ') l++;
		if (!strncmp(l, "Connected to", 12)) connected = 1;
		else if (!strncmp(l, "SSID:", 5)) {
			char raw[512];
			snprintf(raw, sizeof(raw), "%s", l+5);
			trim(raw);
			char name[WIFI_SSID_MAX * 4];
			wifi_ssid_unescape(raw, name, sizeof(name));
			snprintf(ssid, cap, "%s", name);
		}
		else if (!strncmp(l, "signal:", 7) && signal) *signal = (int)strtol(l+7, NULL, 10);
	}
	return connected && ssid[0];
}

int wifi_bars(int dbm) {
	if (dbm>=-55) return 4;
	if (dbm>=-65) return 3;
	if (dbm>=-75) return 2;
	if (dbm>=-85) return 1;
	return 0;
}
const char* wifi_bars_label(int bars) {
	static const char* labels[] = { "Very weak", "Weak", "Fair", "Good", "Excellent" };
	if (bars<0) bars = 0;
	if (bars>4) bars = 4;
	return labels[bars];
}

///////////////////////////////

static void escape_into(FILE* f, const char* s) {
	for (; *s; s++) {
		if (*s=='\\') fputs("\\\\", f);
		else if (*s=='\t') fputs("\\t", f);
		else if (*s=='\n') fputs("\\n", f);
		else if (*s=='\r') fputs("\\r", f);
		else fputc(*s, f);
	}
}
static void unescape_field(char* s) {
	char* w = s;
	for (char* r=s; *r; r++) {
		if (*r=='\\' && r[1]) {
			r++;
			if (*r=='t') *w++ = '\t';
			else if (*r=='n') *w++ = '\n';
			else if (*r=='r') *w++ = '\r';
			else *w++ = *r;
		}
		else *w++ = *r;
	}
	*w = '\0';
}

int wifi_saved_load(const char* path, WifiSaved* out, int max) {
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	int count = 0;
	char line[512];
	while (count<max && fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		while (len && (line[len-1]=='\n' || line[len-1]=='\r')) line[--len] = '\0';
		if (!len || line[0]=='#') continue;
		char* tab = strchr(line, '\t');
		char* pass = "";
		if (tab) { *tab = '\0'; pass = tab+1; }
		unescape_field(line);
		char pass_copy[512];
		snprintf(pass_copy, sizeof(pass_copy), "%s", pass);
		unescape_field(pass_copy);
		if (!line[0] || strlen(line)>=WIFI_SSID_MAX || strlen(pass_copy)>=WIFI_PASS_MAX) continue;
		snprintf(out[count].ssid, sizeof(out[count].ssid), "%s", line);
		snprintf(out[count].pass, sizeof(out[count].pass), "%s", pass_copy);
		count++;
	}
	fclose(f);
	return count;
}

int wifi_saved_store(const char* path, const WifiSaved* list, int count) {
	char tmp[1100];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* f = fopen(tmp, "w");
	if (!f) return -1;
	fputs("# Wi-Fi networks remembered by Tools > Wi-Fi: name, a tab, the password. The newest is first.\n", f);
	for (int i=0; i<count; i++) {
		escape_into(f, list[i].ssid);
		fputc('\t', f);
		escape_into(f, list[i].pass);
		fputc('\n', f);
	}
	int ok = fflush(f)==0;
	if (ok) fsync(fileno(f));
	if (fclose(f)!=0) ok = 0;
	if (!ok) { unlink(tmp); return -1; }
	if (rename(tmp, path)!=0) { unlink(tmp); return -1; }
	return 0;
}

int wifi_saved_find(const WifiSaved* list, int count, const char* ssid) {
	for (int i=0; i<count; i++) if (!strcmp(list[i].ssid, ssid)) return i;
	return -1;
}

int wifi_saved_put(WifiSaved* list, int count, int max, const char* ssid, const char* pass) {
	if (!ssid || !*ssid || strlen(ssid)>=WIFI_SSID_MAX || strlen(pass)>=WIFI_PASS_MAX) return count;
	int at = wifi_saved_find(list, count, ssid);
	if (at<0) {
		if (count>=max) at = count-1; // full: the oldest makes room
		else at = count++;
	}
	for (int i=at; i>0; i--) list[i] = list[i-1];
	snprintf(list[0].ssid, sizeof(list[0].ssid), "%s", ssid);
	snprintf(list[0].pass, sizeof(list[0].pass), "%s", pass);
	return count;
}

int wifi_saved_remove(WifiSaved* list, int count, int index) {
	if (index<0 || index>=count) return count;
	for (int i=index; i<count-1; i++) list[i] = list[i+1];
	return count-1;
}

///////////////////////////////

static uint64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec*1000 + ts.tv_nsec/1000000;
}

int wifi_run(char* const argv[], char* out, size_t cap, int timeout_ms) {
	if (cap) out[0] = '\0';
	int fds[2];
	if (pipe(fds)!=0) return -1;
	pid_t pid = fork();
	if (pid<0) { close(fds[0]); close(fds[1]); return -1; }
	if (pid==0) {
		int nul = open("/dev/null", O_RDWR);
		if (nul>=0) { dup2(nul, 0); dup2(nul, 2); }
		dup2(fds[1], 1);
		for (int fd=3; fd<256; fd++) close(fd);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fds[1]);
	size_t len = 0;
	uint64_t deadline = timeout_ms ? now_ms() + (uint64_t)timeout_ms : 0;
	int timed_out = 0;
	for (;;) {
		int wait = 200;
		if (deadline) {
			uint64_t now = now_ms();
			if (now>=deadline) { timed_out = 1; break; }
			if (deadline-now<200) wait = (int)(deadline-now);
		}
		struct pollfd pfd = { fds[0], POLLIN, 0 };
		int r = poll(&pfd, 1, wait);
		if (r>0) {
			char buf[1024];
			ssize_t n = read(fds[0], buf, sizeof(buf));
			if (n<=0) break;
			if (cap && len+1<cap) {
				size_t take = (size_t)n < cap-1-len ? (size_t)n : cap-1-len;
				memcpy(out+len, buf, take);
				len += take;
				out[len] = '\0';
			}
		}
		else if (r<0 && errno!=EINTR) break;
	}
	close(fds[0]);
	if (timed_out) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); return -2; }
	int status = 0;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

pid_t wifi_spawn(char* const argv[]) {
	pid_t pid = fork();
	if (pid==0) {
		setpgid(0, 0); // so the whole command (and what it started) can be stopped together
		int nul = open("/dev/null", O_RDWR);
		if (nul>=0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
		for (int fd=3; fd<256; fd++) close(fd);
		execvp(argv[0], argv);
		_exit(127);
	}
	return pid;
}
int wifi_reap(pid_t pid, int* status) {
	if (pid<=0) return 1;
	int st = 0;
	pid_t r = waitpid(pid, &st, WNOHANG);
	if (r==0) return 0;
	if (status) *status = (r==pid && WIFEXITED(st)) ? WEXITSTATUS(st) : -1;
	return 1;
}
void wifi_kill(pid_t pid) {
	if (pid<=0) return;
	kill(-pid, SIGTERM);
	kill(pid, SIGTERM);
}

void wifi_ip_address(char* out, size_t cap) {
	char text[512];
	if (cap) out[0] = '\0';
	char* argv[] = { "ip", "-4", "-o", "addr", "show", "wlan0", NULL };
	if (wifi_run(argv, text, sizeof(text), 3000)!=0) return;
	char* inet = strstr(text, "inet ");
	if (!inet) return;
	inet += 5;
	size_t n = strcspn(inet, "/ \n");
	if (n>=cap) n = cap-1;
	memcpy(out, inet, n);
	out[n] = '\0';
}
int wifi_has_route(void) {
	char text[1024];
	char* argv[] = { "ip", "route", NULL };
	if (wifi_run(argv, text, sizeof(text), 3000)!=0) return 0;
	return strncmp(text, "default", 7)==0 || strstr(text, "\ndefault")!=NULL;
}
int wifi_radio_on(void) { // IFF_UP in the interface flags: `wifictl disable` takes the interface down, enable brings it up
	const char* dir = getenv("WIFI_NET_DIR");
	char path[256], text[32] = "";
	snprintf(path, sizeof(path), "%s/wlan0/flags", dir && *dir ? dir : "/sys/class/net");
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	if (!fgets(text, sizeof(text), f)) text[0] = '\0';
	fclose(f);
	return (strtoul(text, NULL, 16) & 1)!=0;
}
int wifi_radio_up(void) {
	const char* dir = getenv("WIFI_NET_DIR");
	char path[256], state[16] = "";
	snprintf(path, sizeof(path), "%s/wlan0/operstate", dir && *dir ? dir : "/sys/class/net");
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	if (!fgets(state, sizeof(state), f)) state[0] = '\0';
	fclose(f);
	return !strncmp(state, "up", 2);
}
