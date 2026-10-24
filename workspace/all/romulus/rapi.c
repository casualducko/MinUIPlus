#include "rapi.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>

///////////////////////////////
// config

static char* trim(char* s) {
	while (*s==' ' || *s=='\t') s++;
	char* e = s + strlen(s);
	while (e>s && (e[-1]==' ' || e[-1]=='\t' || e[-1]=='\r' || e[-1]=='\n')) *--e = '\0';
	if (e-s>=2 && ((s[0]=='"' && e[-1]=='"') || (s[0]=='\'' && e[-1]=='\''))) { s++; e[-1] = '\0'; }
	return s;
}
static int clean_value(const char* s) { // nothing that could end up as a second header line
	for (; *s; s++) if ((unsigned char)*s<0x20 || *s==0x7F) return 0;
	return 1;
}

int rcfg_load(const char* path, RConfig* cfg) {
	memset(cfg, 0, sizeof(*cfg));
	FILE* f = fopen(path, "r");
	if (!f) return RCFG_MISSING;
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char* s = trim(line);
		if (!*s || *s=='#') continue;
		char* eq = strchr(s, '=');
		if (!eq) continue;
		*eq = '\0';
		char* key = trim(s);
		char* val = trim(eq+1);
		if (!clean_value(val)) continue;
		if (!strcmp(key, "url")) snprintf(cfg->url, sizeof(cfg->url), "%s", val);
		else if (!strcmp(key, "token")) snprintf(cfg->token, sizeof(cfg->token), "%s", val);
		else if (!strcmp(key, "insecure_http")) cfg->insecure = atoi(val)!=0;
		else if (!strcmp(key, "map") && cfg->map_count<RCFG_MAX_MAPS) {
			char* colon = strchr(val, ':');
			if (!colon) continue;
			*colon = '\0';
			char* slug = trim(val);
			char* folder = trim(colon+1);
			// a folder name, never a path
			if (!*slug || !*folder || strchr(folder, '/') || strchr(folder, '\\') || !strcmp(folder, "..") || folder[0]=='.') continue;
			snprintf(cfg->map[cfg->map_count].slug, sizeof(cfg->map[0].slug), "%s", slug);
			snprintf(cfg->map[cfg->map_count].folder, sizeof(cfg->map[0].folder), "%s", folder);
			cfg->map_count++;
		}
	}
	fclose(f);

	size_t n = strlen(cfg->url);
	while (n>0 && cfg->url[n-1]=='/') cfg->url[--n] = '\0';
	if (!cfg->url[0] || !cfg->token[0]) return RCFG_INCOMPLETE;
	if (strncasecmp(cfg->url, "https://", 8)) {
		if (!(cfg->insecure && !strncasecmp(cfg->url, "http://", 7))) return RCFG_NEEDS_HTTPS;
	}
	return RCFG_OK;
}
const char* rcfg_message(int code) {
	switch (code) {
		case RCFG_MISSING: return "No server set up yet";
		case RCFG_INCOMPLETE: return "Config needs a url and a token";
		case RCFG_NEEDS_HTTPS: return "The url must start with https://";
		default: return "";
	}
}
int rcfg_write_template(const char* path) {
	if (access(path, F_OK)==0) return 0;
	FILE* f = fopen(path, "w");
	if (!f) return -1;
	fputs(
		"# Romulus server for the Romulus tool. Edit this file on a computer.\n"
		"# The url must be https:// (the token is sent with every request).\n"
		"url=\n"
		"token=\n"
		"\n"
		"# Optional: file a platform from the server in a particular ROM folder,\n"
		"# one line per platform, eg.\n"
		"# map=gba:Game Boy Advance (MGBA)\n"
		, f);
	fclose(f);
	return 0;
}

///////////////////////////////
// systems

static const SysDef systems[] = {
	{ {"nes","famicom","fc","fds","nintendo-entertainment-system","famicom-disk-system"}, "NES", "NES", {"FC"}, {"nes","fds","unf","unif"} },
	{ {"snes","sfc","super-nintendo","super-nintendo-entertainment-system","super-famicom","superfamicom"}, "SNES", "SNES", {"SFC"}, {"sfc","smc"} },
	{ {"gb","gameboy","game-boy"}, "Game Boy", "GB", {"GB"}, {"gb"} },
	{ {"gbc","gameboy-color","game-boy-color"}, "Game Boy Color", "GBC", {"GBC"}, {"gbc","gb"} },
	{ {"gba","gameboy-advance","game-boy-advance"}, "Game Boy Advance", "GBA", {"MGBA","GBA"}, {"gba"} },
	{ {"genesis","megadrive","mega-drive","md","sega-genesis","segagenesis"}, "Genesis", "MD", {"MD"}, {"md","gen","smd","bin"} },
	{ {"sms","mastersystem","master-system","sega-master-system"}, "Sega Master System", "SMS", {"SMS"}, {"sms"} },
	{ {"gg","gamegear","game-gear","sega-game-gear"}, "Sega Game Gear", "GG", {"GG"}, {"gg"} },
	{ {"pce","tg16","turbografx","turbografx-16","pc-engine","supergrafx"}, "TurboGrafx-16", "PCE", {"PCE"}, {"pce"} },
	{ {"ngp","ngpc","neo-geo-pocket","neo-geo-pocket-color"}, "Neo Geo Pocket", "NGPC", {"NGPC","NGP"}, {"ngp","ngc"} },
	{ {"vb","virtualboy","virtual-boy"}, "Virtual Boy", "VB", {"VB"}, {"vb","vboy"} },
	{ {"psx","ps","ps1","playstation","sony-playstation"}, "Playstation", "PS", {"PS"}, {"chd","pbp"} },
	{ {"pokemon-mini","pokemini"}, "Pokemon Mini", "PKM", {"PKM"}, {"min"} },
};
const SysDef* sys_for_slug(const char* slug) {
	if (!slug) return NULL;
	for (size_t i=0; i<sizeof(systems)/sizeof(systems[0]); i++) {
		for (int j=0; j<8 && systems[i].slugs[j]; j++) {
			if (!strcasecmp(systems[i].slugs[j], slug)) return &systems[i];
		}
	}
	return NULL;
}

static int ends_with_tag(const char* name, const char* tag) { // "Name (TAG)"
	char suffix[64];
	snprintf(suffix, sizeof(suffix), " (%s)", tag);
	size_t n = strlen(name), m = strlen(suffix);
	return n>m && !strcmp(name+n-m, suffix);
}
static int make_dir(const char* path) {
	if (mkdir(path, 0777)==0) return 0;
	return errno==EEXIST ? 0 : -1;
}

int rom_dest_dir(const RConfig* cfg, const char* slug, const SysDef* sys, const char* roms_root, RHasEmu has_emu, void* ctx, int create, char* dir, size_t cap) {
	// an explicit map= line wins
	if (cfg) for (int i=0; i<cfg->map_count; i++) {
		if (!strcasecmp(cfg->map[i].slug, slug)) {
			snprintf(dir, cap, "%s/%s", roms_root, cfg->map[i].folder);
			return (create && make_dir(dir)) ? -1 : 0;
		}
	}
	if (!sys) return -1;

	for (int t=0; t<3 && sys->tags[t]; t++) {
		if (has_emu && !has_emu(sys->tags[t], ctx)) continue;
		DIR* d = opendir(roms_root);
		if (d) {
			struct dirent* e;
			while ((e = readdir(d))) {
				if (e->d_name[0]=='.' || !ends_with_tag(e->d_name, sys->tags[t])) continue;
				snprintf(dir, cap, "%s/%s", roms_root, e->d_name);
				struct stat st;
				if (stat(dir, &st)==0 && S_ISDIR(st.st_mode)) { closedir(d); return 0; }
			}
			closedir(d);
		}
	}
	for (int t=0; t<3 && sys->tags[t]; t++) {
		if (has_emu && !has_emu(sys->tags[t], ctx)) continue;
		snprintf(dir, cap, "%s/%s (%s)", roms_root, sys->name, sys->tags[t]);
		if (create) {
			make_dir(roms_root);
			if (make_dir(dir)) return -1;
		}
		return 0;
	}
	return -1;
}

void rom_safe_name(const char* in, const SysDef* sys, char* out, size_t cap) {
	const char* base = in;
	for (const char* s=in; *s; s++) if (*s=='/' || *s=='\\') base = s+1;

	char tmp[256];
	size_t n = 0;
	for (const char* s=base; *s && n<sizeof(tmp)-1; s++) {
		unsigned char ch = *s;
		if (ch<0x20 || ch==0x7F || strchr("\\/:*?\"<>|", ch)) ch = '_';
		else if (ch=='\'' || ch=='`' || ch=='$') ch = '_'; // never a problem for us, but not worth keeping either
		tmp[n++] = ch;
	}
	tmp[n] = '\0';
	// no hidden files, nothing a FAT card strips at the end
	size_t start = 0;
	while (tmp[start]=='.' || tmp[start]==' ') start++;
	while (n>start && (tmp[n-1]=='.' || tmp[n-1]==' ')) tmp[--n] = '\0';
	memmove(tmp, tmp+start, n-start+1);
	n -= start;
	if (n==0) { snprintf(tmp, sizeof(tmp), "rom"); n = 3; }

	// an extension the emulator knows (or zip, which gets unpacked), else add the system's
	int known = 0;
	const char* dot = strrchr(tmp, '.');
	if (dot && dot[1]) {
		if (!strcasecmp(dot+1, "zip")) known = 1;
		for (int i=0; sys && i<10 && sys->exts[i]; i++) if (!strcasecmp(dot+1, sys->exts[i])) known = 1;
	}
	if (!known && sys && sys->exts[0]) {
		if (n+1+strlen(sys->exts[0])<sizeof(tmp)) { strcat(tmp, "."); strcat(tmp, sys->exts[0]); n = strlen(tmp); }
	}
	snprintf(out, cap, "%s", tmp);
	// snprintf may have cut in the middle of a UTF-8 character
	size_t len = strlen(out);
	if (len==cap-1 && len>0) {
		while (len>0 && ((unsigned char)out[len-1]&0xC0)==0x80) len--; // back over continuation bytes
		if (len>0 && (unsigned char)out[len-1]>=0xC0) len--; // and the lead byte
		out[len] = '\0';
	}
}

///////////////////////////////
// requests

void rapi_urlencode(const char* in, char* out, size_t cap) {
	static const char* hex = "0123456789ABCDEF";
	size_t n = 0;
	for (const unsigned char* s=(const unsigned char*)in; *s && n+4<cap; s++) {
		if (isalnum(*s) || *s=='-' || *s=='_' || *s=='.' || *s=='*') out[n++] = *s; // what Java's URLEncoder leaves alone
		else if (*s==' ') out[n++] = '+';
		else { out[n++] = '%'; out[n++] = hex[*s>>4]; out[n++] = hex[*s&15]; }
	}
	out[n] = '\0';
}
void rapi_platforms_path(char* out, size_t cap) { snprintf(out, cap, "/api/platforms"); }
void rapi_roms_path(char* out, size_t cap, const char* platform, const char* platforms_csv, const char* query, int offset, int limit) {
	char a[512], q[512];
	size_t n = snprintf(out, cap, "/api/roms?");
	if (platform && *platform) { rapi_urlencode(platform, a, sizeof(a)); n += snprintf(out+n, cap-n, "platform=%s&", a); }
	else if (platforms_csv && *platforms_csv) { rapi_urlencode(platforms_csv, a, sizeof(a)); n += snprintf(out+n, cap-n, "platforms=%s&", a); }
	rapi_urlencode(query ? query : "", q, sizeof(q));
	snprintf(out+n, cap-n, "q=%s&offset=%d&limit=%d&one=1", q, offset, limit);
}
void rapi_download_path(char* out, size_t cap, long long id) { snprintf(out, cap, "/api/roms/%lld/download", id); }

///////////////////////////////
// the curl job

static int job_counter = 0;

static void read_small(const char* path, char* buf, size_t cap) {
	buf[0] = '\0';
	FILE* f = fopen(path, "r");
	if (!f) return;
	size_t n = fread(buf, 1, cap-1, f);
	buf[n] = '\0';
	fclose(f);
}

int rjob_start(RJob* job, const RConfig* cfg, const char* api_path, const char* out_path, int download) {
	memset(job, 0, sizeof(*job));
	const char* tmp = getenv("TMPDIR");
	if (!tmp || !*tmp) tmp = "/tmp";
	int id = ++job_counter;
	snprintf(job->out_path, sizeof(job->out_path), "%s", out_path);
	snprintf(job->code_path, sizeof(job->code_path), "%s/romulus-%d-%d.code", tmp, (int)getpid(), id);
	snprintf(job->err_path, sizeof(job->err_path), "%s/romulus-%d-%d.err", tmp, (int)getpid(), id);
	snprintf(job->hdr_path, sizeof(job->hdr_path), "%s/romulus-%d-%d.hdr", tmp, (int)getpid(), id);

	char url[1024];
	if (snprintf(url, sizeof(url), "%s%s", cfg->url, api_path)>=(int)sizeof(url)) return -1;

	// the token goes in a private file, not on the command line where ps would show it
	int fd = open(job->hdr_path, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	if (fd<0) return -1;
	char hdr[512];
	int n = snprintf(hdr, sizeof(hdr), "Authorization: Bearer %s\n", cfg->token);
	if (n<=0 || n>=(int)sizeof(hdr) || write(fd, hdr, n)!=n) { close(fd); unlink(job->hdr_path); return -1; }
	close(fd);

	char hdr_arg[600];
	snprintf(hdr_arg, sizeof(hdr_arg), "@%s", job->hdr_path);

	const char* argv[32];
	int a = 0;
	argv[a++] = "curl";
	argv[a++] = "-sS";
	argv[a++] = "-L";
	argv[a++] = "--max-redirs"; argv[a++] = "3";
	argv[a++] = "--proto"; argv[a++] = cfg->insecure ? "=http,https" : "=https";
	argv[a++] = "--proto-redir"; argv[a++] = cfg->insecure ? "=http,https" : "=https";
	argv[a++] = "--connect-timeout"; argv[a++] = "10";
	if (download) { argv[a++] = "--speed-limit"; argv[a++] = "1024"; argv[a++] = "--speed-time"; argv[a++] = "30"; } // gives up only when it stalls
	else { argv[a++] = "--max-time"; argv[a++] = "30"; }
	argv[a++] = "-H"; argv[a++] = hdr_arg;
	argv[a++] = "-o"; argv[a++] = job->out_path;
	argv[a++] = "-w"; argv[a++] = "%{http_code}";
	argv[a++] = "--";
	argv[a++] = url;
	argv[a] = NULL;

	unlink(job->out_path);
	pid_t pid = fork();
	if (pid<0) { unlink(job->hdr_path); return -1; }
	if (pid==0) {
		int code = open(job->code_path, O_WRONLY|O_CREAT|O_TRUNC, 0600);
		int err = open(job->err_path, O_WRONLY|O_CREAT|O_TRUNC, 0600);
		int nul = open("/dev/null", O_RDONLY);
		if (nul>=0) dup2(nul, 0);
		if (code>=0) dup2(code, 1);
		if (err>=0) dup2(err, 2);
		execvp("curl", (char* const*)argv);
		_exit(127);
	}
	job->pid = pid;
	return 0;
}

int rjob_poll(RJob* job) {
	if (job->done) return 1;
	if (job->pid<=0) return 1;
	int st = 0;
	pid_t r = waitpid(job->pid, &st, WNOHANG);
	if (r==0) return 0;
	job->done = 1;
	job->curl_exit = (r<0) ? -1 : (WIFEXITED(st) ? WEXITSTATUS(st) : 128+WTERMSIG(st));
	char buf[16];
	read_small(job->code_path, buf, sizeof(buf));
	job->http = atoi(buf);
	unlink(job->hdr_path);
	return 1;
}
void rjob_cancel(RJob* job) {
	if (job->done || job->pid<=0) return;
	kill(job->pid, SIGTERM);
	waitpid(job->pid, NULL, 0);
	job->done = 1;
	job->curl_exit = -2; // cancelled
	job->http = 0;
	unlink(job->hdr_path);
}
long rjob_bytes(const RJob* job) {
	struct stat st;
	return stat(job->out_path, &st)==0 ? (long)st.st_size : 0;
}
const char* rjob_error(const RJob* job) {
	if (job->curl_exit==-2) return "Cancelled";
	if (job->curl_exit!=0) {
		switch (job->curl_exit) {
			case 127: return "curl isn't available on this device";
			case 6: return "Can't find the server (is Wi-Fi on?)";
			case 7: return "Can't connect to the server";
			case 28: return "The server took too long";
			case 23: return "Can't write to the card (full?)";
			case 35: case 51: case 53: case 54: case 58: case 59: case 60: case 77: case 82: case 83: case 90: case 91:
				return "Secure connection failed (check the url and the device's date and time)";
			case 1: case 3: case 5: return "The server address looks wrong";
			default: return "Connection lost";
		}
	}
	if (job->http==200) return NULL;
	if (job->http==401) return "Wrong token";
	if (job->http==404) return "Server has no API (is ROMULUS_API_TOKEN set, and /api/ exempt from the proxy login?)";
	static char msg[64];
	snprintf(msg, sizeof(msg), "Server answered %d", job->http);
	return msg;
}
void rjob_cleanup(RJob* job) {
	unlink(job->code_path);
	unlink(job->err_path);
	unlink(job->hdr_path);
}

///////////////////////////////
// answers

static void copy_str(char* dst, size_t cap, const char* src) { snprintf(dst, cap, "%s", src ? src : ""); }

int rapi_parse_platforms(const char* body, size_t len, RPlat* out, int max) {
	JVal* root = json_parse(body, len);
	if (!root || root->type!=J_ARR) { json_free(root); return -1; }
	int n = 0;
	for (int i=0; i<root->count && n<max; i++) {
		JVal* p = root->items[i];
		const char* slug = json_str(p, "slug", NULL);
		if (!slug || !*slug) continue;
		copy_str(out[n].slug, sizeof(out[n].slug), slug);
		out[n].count = (int)json_int(p, "count", 0);
		n++;
	}
	json_free(root);
	return n;
}

int rapi_parse_roms(const char* body, size_t len, RRomList* list, long* total, int* raw) {
	JVal* root = json_parse(body, len);
	if (!root || root->type!=J_OBJ) { json_free(root); return -1; }
	JVal* roms = json_get(root, "roms");
	if (!roms || roms->type!=J_ARR) { json_free(root); return -1; }
	if (total) *total = (long)json_int(root, "total", 0);
	if (raw) *raw = roms->count;
	int added = 0;
	for (int i=0; i<roms->count; i++) {
		JVal* r = roms->items[i];
		long long id = json_int(r, "id", -1);
		const char* file_name = json_str(r, "file_name", "");
		if (id<0 || !*file_name) continue; // can't download it without both
		if (list->count==list->cap) {
			int cap = list->cap ? list->cap*2 : 64;
			RRom* items = realloc(list->items, sizeof(RRom)*cap);
			if (!items) break;
			list->items = items;
			list->cap = cap;
		}
		RRom* o = &list->items[list->count++];
		memset(o, 0, sizeof(*o));
		o->id = id;
		copy_str(o->file_name, sizeof(o->file_name), file_name);
		copy_str(o->name, sizeof(o->name), json_str(r, "name", file_name));
		copy_str(o->platform, sizeof(o->platform), json_str(r, "platform", ""));
		copy_str(o->region, sizeof(o->region), json_str(r, "region", ""));
		o->size = json_int(r, "size", 0);
		added++;
	}
	json_free(root);
	return added;
}
void rromlist_free(RRomList* list) {
	free(list->items);
	list->items = NULL;
	list->count = list->cap = 0;
}

///////////////////////////////
// ledger

int ledger_has(const char* path, long long id) {
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	char line[64];
	int found = 0;
	while (fgets(line, sizeof(line), f)) {
		if (atoll(line)==id && isdigit((unsigned char)line[0])) { found = 1; break; }
	}
	fclose(f);
	return found;
}
void ledger_add(const char* path, long long id) {
	if (ledger_has(path, id)) return;
	FILE* f = fopen(path, "a");
	if (!f) return;
	fprintf(f, "%lld\n", id);
	fclose(f);
}
