#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "bbs.h"

#define DEFAULT_BASE "https://www.lexaloffle.com"
static const char* base_url(void) { const char* e = getenv("PICO_BBS_BASE"); return (e && *e) ? e : DEFAULT_BASE; }

void bbs_urlencode(const char* in, char* out, size_t cap) {
	static const char* hex = "0123456789ABCDEF";
	size_t n = 0;
	for (; *in && n+4<cap; in++) {
		unsigned char c = (unsigned char)*in;
		if (isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~') out[n++] = c;
		else { out[n++] = '%'; out[n++] = hex[c>>4]; out[n++] = hex[c&15]; }
	}
	out[n] = '\0';
}
void bbs_search_url(char* out, size_t cap, const char* query, int page) {
	char q[512];
	bbs_urlencode(query ? query : "", q, sizeof(q));
	if (page<1) page = 1;
	snprintf(out, cap, "%s/bbs/?cat=7&carts_tab=1&page=%d&search=%s", base_url(), page, q);
}
void bbs_browse_url(char* out, size_t cap, BbsBrowse kind, const char* tag, int page) {
	if (page<1) page = 1;
	const char* order = "featured";
	char extra[96] = "";
	switch (kind) {
		case BBS_BROWSE_POPULAR: order = "ts"; snprintf(extra, sizeof(extra), "&popular=1"); break; // popular right now
		case BBS_BROWSE_NEWEST: order = "ts"; break;
		case BBS_BROWSE_RANDOM: order = "lucky"; break;
		case BBS_BROWSE_TAG: {
			char t[64];
			bbs_urlencode(tag ? tag : "", t, sizeof(t));
			snprintf(extra, sizeof(extra), "&tag=%s", t);
			break;
		}
		default: break;
	}
	snprintf(out, cap, "%s/bbs/lister.php?use_hurl=1&cat=7&sub=2&orderby=%s&page=%d%s", base_url(), order, page, extra);
}
const char* bbs_browse_name(BbsBrowse kind, const char* tag) {
	static char name[64];
	switch (kind) {
		case BBS_BROWSE_POPULAR: return "Popular now";
		case BBS_BROWSE_NEWEST: return "New and updated";
		case BBS_BROWSE_RANDOM: return "Random";
		case BBS_BROWSE_TAG: snprintf(name, sizeof(name), "#%s", tag ? tag : ""); return name;
		default: return "Featured";
	}
}
static const char* const tags[] = { // the common ones on the BBS, each seen to have a full page of carts
	"platformer", "puzzle", "arcade", "adventure", "action", "shooter", "shmup", "roguelike", "metroidvania", "rpg",
	"strategy", "racing", "fighting", "sports", "horror", "music", "tool", "3d", "fps", "sokoban", "demake", "mod",
	"tetris", "snake", "cards", "bullet-hell", "maze", "retro", "simulation", "tutorial", "jam", "turn-based", "rts",
	"story", "multiplayer", "cute", "space", "top-down", "2player",
};
const char* const* bbs_tag_list(int* count) { *count = (int)(sizeof(tags)/sizeof(tags[0])); return tags; }
void bbs_thread_url(char* out, size_t cap, long tid) { snprintf(out, cap, "%s/bbs/?tid=%ld", base_url(), tid); }
void bbs_thumb_url(char* out, size_t cap, const char* thumb_path) { snprintf(out, cap, "%s%s", base_url(), thumb_path); }
void bbs_cart_url(char* out, size_t cap, const char* cart_path) { snprintf(out, cap, "%s%s", base_url(), cart_path); }

///////////////////////////////
// the listing: pdat=[ ['pid', tid, `title`,"thumb",w,h,"created",uid,"author","last",uid,"who",stars,comments,...,'x',["tag",...],...], ... ];

static void entity_decode(char* s) { // &amp; &lt; &gt; &quot; &#39; &#NNN; in place (never longer than before)
	char* w = s;
	for (char* r = s; *r; ) {
		if (*r=='&') {
			static const struct { const char* name; char c; } named[] = { {"&amp;",'&'}, {"&lt;",'<'}, {"&gt;",'>'}, {"&quot;",'"'}, {"&apos;",'\''}, {"&#39;",'\''} };
			int done = 0;
			for (int i=0; i<(int)(sizeof(named)/sizeof(named[0])); i++) {
				size_t n = strlen(named[i].name);
				if (!strncmp(r, named[i].name, n)) { *w++ = named[i].c; r += n; done = 1; break; }
			}
			if (done) continue;
			if (r[1]=='#') {
				char* end;
				long cp = strtol(r+2, &end, 10);
				if (*end==';' && cp>0 && cp<0x110000 && end-r<=9) {
					char u[4]; int n;
					if (cp<0x80) { u[0] = (char)cp; n = 1; }
					else if (cp<0x800) { u[0] = 0xC0 | (cp>>6); u[1] = 0x80 | (cp&0x3F); n = 2; }
					else if (cp<0x10000) { u[0] = 0xE0 | (cp>>12); u[1] = 0x80 | ((cp>>6)&0x3F); u[2] = 0x80 | (cp&0x3F); n = 3; }
					else n = 0; // (four-byte ones would not fit the slot they came from)
					if (n && w+n<=end+1) { memcpy(w, u, n); w += n; r = end+1; continue; }
				}
			}
		}
		*w++ = *r++;
	}
	*w = '\0';
}

// A backslash escape inside a JavaScript string, with p at the backslash: \n \t \uXXXX, or the character itself
// (\\ \` \' \"). Writes what it means to out (UTF-8) and returns the position after it.
static const char* js_escape(const char* p, const char* end, char* out, size_t* n, size_t cap) {
	p++; // the backslash
	if (p>=end) return p;
	char c = *p++;
	unsigned cp = 0;
	int is_cp = 0;
	if (c=='n') { cp = '\n'; is_cp = 1; }
	else if (c=='t') { cp = '\t'; is_cp = 1; }
	else if (c=='u' && p+4<=end) {
		char hex[5] = { p[0], p[1], p[2], p[3], 0 };
		char* stop;
		unsigned long v = strtoul(hex, &stop, 16);
		if (!*stop) { cp = (unsigned)v; is_cp = 1; p += 4; }
	}
	if (!is_cp) { if (*n+1<cap) out[(*n)++] = c; return p; }
	if (cp<0x80) { if (*n+1<cap) out[(*n)++] = (char)cp; }
	else if (cp<0x800) { if (*n+2<cap) { out[(*n)++] = 0xC0 | (cp>>6); out[(*n)++] = 0x80 | (cp&0x3F); } }
	else { if (*n+3<cap) { out[(*n)++] = 0xE0 | (cp>>12); out[(*n)++] = 0x80 | ((cp>>6)&0x3F); out[(*n)++] = 0x80 | (cp&0x3F); } }
	return p;
}

// One cell of a row, as text: strings without their quotes (escapes undone), numbers as written, an
// array as its strings joined by ", ", an empty slot as "". Returns the position after the cell.
static const char* read_cell(const char* p, const char* end, char* out, size_t cap) {
	size_t n = 0;
	out[0] = '\0';
	while (p<end && (*p==' ' || *p=='\t' || *p=='\n' || *p=='\r')) p++;
	if (p>=end) return p;
	if (*p=='\'' || *p=='"' || *p=='`') {
		char q = *p++;
		while (p<end && *p!=q) {
			if (*p=='\\') { p = js_escape(p, end, out, &n, cap); continue; }
			if (n+1<cap) out[n++] = *p;
			p++;
		}
		if (p<end) p++;
		out[n] = '\0';
		entity_decode(out);
		return p;
	}
	if (*p=='[') {
		int depth = 0;
		int in_str = 0;
		char q = 0;
		const char* start = p;
		for (; p<end; p++) {
			if (in_str) { if (*p=='\\' && p+1<end) p++; else if (*p==q) in_str = 0; continue; }
			if (*p=='"' || *p=='\'' || *p=='`') { in_str = 1; q = *p; }
			else if (*p=='[') depth++;
			else if (*p==']' && --depth==0) { p++; break; }
		}
		// the strings inside
		int first = 1;
		for (const char* s = start; s<p; s++) {
			if (*s=='"' || *s=='\'' || *s=='`') {
				char sq = *s++;
				if (!first && n+2<cap) { out[n++] = ','; out[n++] = ' '; }
				first = 0;
				while (s<p && *s!=sq) {
					if (*s=='\\') { s = js_escape(s, p, out, &n, cap); continue; }
					if (n+1<cap) out[n++] = *s;
					s++;
				}
			}
		}
		out[n] = '\0';
		entity_decode(out);
		return p;
	}
	while (p<end && *p!=',' && *p!=']') { if (n+1<cap && !isspace((unsigned char)*p)) out[n++] = *p; p++; }
	out[n] = '\0';
	return p;
}

static void put(char* dst, size_t cap, const char* src) { snprintf(dst, cap, "%s", src); }

int bbs_parse_search(const char* html, size_t len, BbsCart* out, int max) {
	const char* end = html + len;
	const char* p = NULL;
	for (const char* s = html; s+6<=end; s++) if (!memcmp(s, "pdat=[", 6)) { p = s + 6; break; }
	if (!p) return -1;
	int count = 0;
	while (p<end && count<max) {
		while (p<end && *p!='[' && *p!=']') p++; // to the next row, or the end of the list
		if (p>=end || *p==']') break;
		p++; // the row's [
		char cell[256];
		BbsCart c;
		memset(&c, 0, sizeof(c));
		int idx = 0;
		int ok = 1;
		while (p<end) {
			p = read_cell(p, end, cell, sizeof(cell));
			switch (idx) {
				case 0: put(c.pid, sizeof(c.pid), cell); break;
				case 1: c.tid = atol(cell); break;
				case 2: put(c.title, sizeof(c.title), cell); break;
				case 3: // only a plain file under /bbs/thumbs/ (the path is put on the BBS address and used by the fetch)
					if (!strncmp(cell, "/bbs/thumbs/", 12) && !strstr(cell, "..") && strlen(cell)<sizeof(c.thumb) && strspn(cell, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/._-")==strlen(cell)) put(c.thumb, sizeof(c.thumb), cell);
					break;
				case 6: put(c.date, sizeof(c.date), cell); break;
				case 8: put(c.author, sizeof(c.author), cell); break;
				case 12: c.stars = atoi(cell); break;
				case 18: put(c.tags, sizeof(c.tags), cell); break;
			}
			idx++;
			while (p<end && (*p==' ' || *p=='\t' || *p=='\n' || *p=='\r')) p++;
			if (p<end && *p==',') { p++; continue; }
			if (p<end && *p==']') { p++; break; }
			ok = 0; break; // not a row we understand
		}
		if (!ok || idx<13) { // skip to the next row (they start with ['), so one odd row does not end the page
			while (p<end && !(p[0]=='[' && p+1<end && p[1]=='\'')) p++;
			continue;
		}
		if (c.tid>0 && c.title[0]) out[count++] = c;
	}
	return count;
}

int bbs_parse_cart_path(const char* html, size_t len, char* path, size_t cap) {
	static const char* marker = "/bbs/cposts/";
	size_t mlen = strlen(marker);
	for (size_t i=0; i+7<len; i++) {
		if (memcmp(html+i, "href=", 5)) continue;
		char q = html[i+5];
		if (q!='"' && q!='\'') continue;
		size_t s = i + 6, e = s;
		while (e<len && html[e]!=q && html[e]!='\n' && e-s<300) e++;
		if (e>=len || html[e]!=q) continue;
		size_t m = s; // the path part of the link: after a scheme and host, if there is one
		while (m+mlen<=e && memcmp(html+m, marker, mlen)) m++;
		if (m+mlen>e) continue;
		size_t n = e - m;
		if (n<20 || n>=cap || n>200 || memcmp(html+e-7, ".p8.png", 7)) continue;
		int safe = 1;
		for (size_t k=0; k<n; k++) { unsigned char ch = html[m+k]; if (!(isalnum(ch) || ch=='/' || ch=='.' || ch=='_' || ch=='-')) safe = 0; }
		if (!safe) continue;
		memcpy(path, html+m, n);
		path[n] = '\0';
		return 0;
	}
	return -1;
}

///////////////////////////////
// ranking and names

static int has_word(const char* hay, const char* word, size_t wlen) { // case-insensitive substring
	if (!wlen) return 1;
	for (const char* p = hay; *p; p++) if (!strncasecmp(p, word, wlen)) return 1;
	return 0;
}
int bbs_match_tier(const BbsCart* c, const char* query) {
	if (!query || !*query) return 0;
	char q[128];
	snprintf(q, sizeof(q), "%s", query);
	size_t qn = strlen(q);
	while (qn && isspace((unsigned char)q[qn-1])) q[--qn] = '\0';
	const char* qs = q;
	while (*qs && isspace((unsigned char)*qs)) qs++;
	if (!*qs) return 0;
	if (has_word(c->title, qs, strlen(qs))) return 3;
	int in_title = 1, in_any = 1;
	for (const char* p = qs; *p; ) {
		while (*p==' ') p++;
		if (!*p) break;
		size_t n = 0;
		while (p[n] && p[n]!=' ') n++;
		if (!has_word(c->title, p, n)) in_title = 0;
		if (!has_word(c->title, p, n) && !has_word(c->tags, p, n)) in_any = 0;
		p += n;
	}
	return in_title ? 2 : in_any ? 1 : 0;
}
const char* bbs_sort_name(BbsSort s) {
	return s==BBS_SORT_STARS ? "Most stars" : s==BBS_SORT_NEWEST ? "Newest" : s==BBS_SORT_LISTED ? "As listed" : "Best match";
}
typedef struct { BbsCart c; int tier; int order; } Ranked;
static BbsSort sort_how;
static int cmp_ranked(const void* a, const void* b) {
	const Ranked* x = a; const Ranked* y = b;
	int r = 0;
	if (sort_how==BBS_SORT_BEST) r = y->tier - x->tier, r = r ? r : y->c.stars - x->c.stars;
	else if (sort_how==BBS_SORT_STARS) r = y->c.stars - x->c.stars;
	else if (sort_how==BBS_SORT_LISTED) r = x->c.order - y->c.order;
	else r = strcmp(y->c.date, x->c.date);
	return r ? r : x->order - y->order; // stable
}
void bbs_sort(BbsCart* carts, int count, const char* query, BbsSort how) {
	if (count<2) return;
	Ranked* r = malloc(sizeof(Ranked) * count);
	if (!r) return;
	for (int i=0; i<count; i++) { r[i].c = carts[i]; r[i].tier = bbs_match_tier(&carts[i], query); r[i].order = i; }
	sort_how = how;
	qsort(r, count, sizeof(Ranked), cmp_ranked);
	for (int i=0; i<count; i++) carts[i] = r[i].c;
	free(r);
}
void bbs_file_name(const char* title, const char* suffix, char* out, size_t cap) {
	char name[200];
	size_t n = 0;
	int space = 1; // no leading space
	for (const unsigned char* p = (const unsigned char*)title; *p && n+1<sizeof(name)-1; p++) {
		unsigned char c = *p;
		if (c<0x20 || c==0x7F || strchr("/\\:*?\"<>|", c)) c = ' ';
		if (c==' ') { if (space) continue; space = 1; }
		else if (c=='.' && n==0) continue; // a leading dot would hide the file from the launcher
		else space = 0;
		name[n++] = c;
	}
	while (n && (name[n-1]==' ' || name[n-1]=='.')) n--;
	if (n>80) { // cut between UTF-8 characters
		n = 80;
		while (n>0 && ((unsigned char)name[n]&0xC0)==0x80) n--;
		while (n && name[n-1]==' ') n--;
	}
	name[n] = '\0';
	if (!n) snprintf(name, sizeof(name), "cart");
	snprintf(out, cap, "%s%s%s.p8.png", name, suffix && *suffix ? " " : "", suffix ? suffix : "");
}
int bbs_is_png(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return 0;
	unsigned char sig[8];
	size_t got = fread(sig, 1, 8, f);
	fclose(f);
	struct stat st;
	return got==8 && !memcmp(sig, "\x89PNG\r\n\x1a\n", 8) && stat(path, &st)==0 && st.st_size>=1024;
}

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
int bbs_job_start(BbsJob* job, const char* url, const char* out_path, int download) {
	memset(job, 0, sizeof(*job));
	const char* tmp = getenv("TMPDIR");
	if (!tmp || !*tmp) tmp = "/tmp";
	int id = ++job_counter;
	snprintf(job->out_path, sizeof(job->out_path), "%s", out_path);
	snprintf(job->code_path, sizeof(job->code_path), "%s/pico-%d-%d.code", tmp, (int)getpid(), id);
	snprintf(job->err_path, sizeof(job->err_path), "%s/pico-%d-%d.err", tmp, (int)getpid(), id);
	int insecure = !strncmp(url, "http://", 7); // only a test server (PICO_BBS_BASE) is ever plain http
	const char* argv[32];
	int a = 0;
	argv[a++] = "curl";
	argv[a++] = "-sS";
	argv[a++] = "-L";
	argv[a++] = "--max-redirs"; argv[a++] = "3";
	argv[a++] = "--proto"; argv[a++] = insecure ? "=http,https" : "=https";
	argv[a++] = "--proto-redir"; argv[a++] = insecure ? "=http,https" : "=https";
	argv[a++] = "--connect-timeout"; argv[a++] = "10";
	if (download) { argv[a++] = "--speed-limit"; argv[a++] = "1024"; argv[a++] = "--speed-time"; argv[a++] = "30"; }
	else { argv[a++] = "--max-time"; argv[a++] = "40"; argv[a++] = "--compressed"; }
	argv[a++] = "--max-filesize"; argv[a++] = "8000000"; // a page or a cart: never more than this
	argv[a++] = "-A"; argv[a++] = "MinUIPlus-PicoSearch/1.0 (a handheld cart browser; one request at a time)";
	argv[a++] = "-o"; argv[a++] = job->out_path;
	argv[a++] = "-w"; argv[a++] = "%{http_code}";
	argv[a++] = "--";
	argv[a++] = url;
	argv[a] = NULL;
	unlink(job->out_path);
	pid_t pid = fork();
	if (pid<0) return -1;
	if (pid==0) {
		unlink(job->code_path); // (made fresh: never a leftover, never somebody's link)
		unlink(job->err_path);
		int code = open(job->code_path, O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW, 0600);
		int err = open(job->err_path, O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW, 0600);
		int nul = open("/dev/null", O_RDONLY);
		if (nul>=0) dup2(nul, 0);
		if (code>=0) dup2(code, 1);
		if (err>=0) dup2(err, 2);
		for (int fd=3; fd<256; fd++) close(fd);
		execvp("curl", (char* const*)argv);
		_exit(127);
	}
	job->pid = pid;
	return 0;
}
int bbs_job_poll(BbsJob* job) {
	if (job->done || job->pid<=0) return 1;
	int st = 0;
	pid_t r = waitpid(job->pid, &st, WNOHANG);
	if (r==0) return 0;
	job->done = 1;
	job->curl_exit = (r<0) ? -1 : (WIFEXITED(st) ? WEXITSTATUS(st) : 128+WTERMSIG(st));
	char buf[16];
	read_small(job->code_path, buf, sizeof(buf));
	job->http = atoi(buf);
	return 1;
}
void bbs_job_cancel(BbsJob* job) {
	if (job->done || job->pid<=0) return;
	kill(job->pid, SIGTERM);
	waitpid(job->pid, NULL, 0);
	job->done = 1;
	job->curl_exit = -2;
	job->http = 0;
}
const char* bbs_job_error(const BbsJob* job) {
	if (job->curl_exit==-2) return "Cancelled";
	if (job->curl_exit!=0) {
		switch (job->curl_exit) {
			case 127: return "curl isn't available on this device";
			case 6: return "Can't find lexaloffle.com (is Wi-Fi on?)";
			case 7: return "Can't connect to lexaloffle.com";
			case 28: return "lexaloffle.com took too long";
			case 23: return "Can't write to the card (full?)";
			case 35: case 51: case 53: case 54: case 58: case 59: case 60: case 77: case 82: case 83: case 90: case 91:
				return "Secure connection failed (check the device's date and time)";
			default: return "Connection lost";
		}
	}
	if (job->http==200) return NULL;
	static char msg[64];
	snprintf(msg, sizeof(msg), job->http==404 ? "Not found on lexaloffle.com" : "lexaloffle.com answered %d", job->http);
	return msg;
}
void bbs_job_cleanup(BbsJob* job) {
	unlink(job->code_path);
	unlink(job->err_path);
}
long bbs_job_bytes(const BbsJob* job) {
	struct stat st;
	return stat(job->out_path, &st)==0 ? (long)st.st_size : 0;
}
