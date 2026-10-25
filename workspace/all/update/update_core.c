#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "update_core.h"

static char* trim(char* s) {
	while (*s==' ' || *s=='\t') s++;
	char* e = s + strlen(s);
	while (e>s && (e[-1]==' ' || e[-1]=='\t' || e[-1]=='\r' || e[-1]=='\n')) *--e = '\0';
	return s;
}
void Update_parseConfig(const char* text, char* repo, size_t repo_size, char* token, size_t token_size) {
	if (repo_size) snprintf(repo, repo_size, "%s", UPDATE_DEFAULT_REPO);
	if (token_size) token[0] = '\0';
	if (!text) return;
	const char* p = text;
	while (*p) {
		const char* nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		char line[512];
		snprintf(line, sizeof(line), "%.*s", (int)(len < sizeof(line)-1 ? len : sizeof(line)-1), p);
		p += len + (nl ? 1 : 0);
		char* l = trim(line);
		if (*l=='#' || !*l) continue;
		char* eq = strchr(l, '=');
		if (!eq) continue;
		*eq = '\0';
		char* key = trim(l);
		char* value = trim(eq+1);
		if (!strcmp(key, "repo") && *value && repo_size) snprintf(repo, repo_size, "%s", value);
		else if (!strcmp(key, "token") && token_size) snprintf(token, token_size, "%s", value);
	}
}

// the string after `"key":"` starting the search at from, copied into out; NULL if not found before end
static const char* jsonString(const char* from, const char* end, const char* key, char* out, size_t size) {
	char pat[48];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char* p = from;
	while ((p = strstr(p, pat)) && (!end || p<end)) {
		const char* q = p + strlen(pat);
		while (*q==' ' || *q=='\t' || *q=='\n') q++;
		if (*q!=':') { p = q; continue; }
		q++;
		while (*q==' ' || *q=='\t' || *q=='\n') q++;
		if (*q!='"') { p = q; continue; }
		q++;
		size_t n = 0;
		while (*q && *q!='"' && n+1<size) { if (*q=='\\' && q[1]) q++; out[n++] = *q++; }
		out[n] = '\0';
		return p;
	}
	return NULL;
}
static int endsWith(const char* s, const char* suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a>=b && !strcmp(s+a-b, suffix);
}
int Update_parseRelease(const char* json, UpdateRelease* out) {
	memset(out, 0, sizeof(*out));
	if (!json) return 0;
	if (!jsonString(json, NULL, "tag_name", out->tag, sizeof(out->tag)) || !out->tag[0]) return 0;
	// each asset has an "url" ending in /releases/assets/<id>; its name and size follow before the next one
	const char* starts[64];
	int n = 0;
	const char* p = json;
	while (n<64 && (p = strstr(p, "/releases/assets/"))) {
		const char* open = p;
		while (open>json && *open!='"') open--; // the opening quote of the url
		starts[n++] = open;
		p += 17;
	}
	typedef struct { char name[160]; char url[400]; uint64_t size; } Asset;
	Asset* assets = calloc(n ? n : 1, sizeof(Asset));
	if (!assets) return 0;
	for (int i=0; i<n; i++) {
		const char* end = i+1<n ? starts[i+1] : NULL;
		const char* u = starts[i] + 1;
		size_t len = 0;
		while (u[len] && u[len]!='"' && len<sizeof(assets[i].url)-1) len++;
		memcpy(assets[i].url, u, len);
		assets[i].url[len] = '\0';
		jsonString(starts[i], end, "name", assets[i].name, sizeof(assets[i].name));
		const char* s = strstr(starts[i], "\"size\"");
		if (s && (!end || s<end)) {
			s = strchr(s, ':');
			if (s) assets[i].size = strtoull(s+1, NULL, 10);
		}
	}
	int found = 0;
	for (int i=0; i<n; i++) {
		if (!endsWith(assets[i].name, ".tar.gz")) continue;
		snprintf(out->pkg_name, sizeof(out->pkg_name), "%s", assets[i].name);
		snprintf(out->pkg_url, sizeof(out->pkg_url), "%s", assets[i].url);
		out->pkg_size = assets[i].size;
		char want[200];
		snprintf(want, sizeof(want), "%s.sha256", assets[i].name);
		for (int j=0; j<n; j++) if (!strcmp(assets[j].name, want)) { snprintf(out->sum_url, sizeof(out->sum_url), "%s", assets[j].url); break; }
		found = out->sum_url[0] != '\0';
		break;
	}
	free(assets);
	return found;
}

static int numbers(const char* s, unsigned long long* v, int max) {
	int n = 0;
	while (*s && n<max) {
		if (isdigit((unsigned char)*s)) {
			unsigned long long x = 0;
			while (isdigit((unsigned char)*s)) { x = x*10 + (*s-'0'); s++; }
			v[n++] = x;
		}
		else s++;
	}
	return n;
}
int Update_compareVersions(const char* a, const char* b) {
	unsigned long long x[12] = {0}, y[12] = {0};
	numbers(a ? a : "", x, 12);
	numbers(b ? b : "", y, 12);
	for (int i=0; i<12; i++) if (x[i]!=y[i]) return x[i]<y[i] ? -1 : 1;
	return 0;
}
int Update_parseSum(const char* text, char* out) {
	if (!text) return 0;
	while (*text==' ' || *text=='\n' || *text=='\r' || *text=='\t') text++;
	for (int i=0; i<64; i++) {
		if (!isxdigit((unsigned char)text[i])) return 0;
		out[i] = (char)tolower((unsigned char)text[i]);
	}
	if (isxdigit((unsigned char)text[64])) return 0; // longer than a sha-256
	out[64] = '\0';
	return 1;
}
int Update_pathsSafe(const char* listing) {
	if (!listing) return 0;
	const char* p = listing;
	int lines = 0;
	while (*p) {
		const char* nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		if (len) {
			lines++;
			if (p[0]=='/') return 0;
			for (size_t i=0; i<len; i++) {
				if (p[i]=='.' && i+1<len && p[i+1]=='.' && (i==0 || p[i-1]=='/') && (i+2==len || p[i+2]=='/')) return 0;
				if (p[i]=='\0') return 0;
			}
		}
		p += len + (nl ? 1 : 0);
	}
	return lines>0;
}
int Update_repoValid(const char* repo) {
	if (!repo || !*repo) return 0;
	int slashes = 0;
	size_t n = strlen(repo);
	if (n>100) return 0;
	for (size_t i=0; i<n; i++) {
		char c = repo[i];
		if (c=='/') { slashes++; if (i==0 || i==n-1) return 0; }
		else if (!(isalnum((unsigned char)c) || c=='-' || c=='_' || c=='.')) return 0;
	}
	return slashes==1 && strstr(repo, "..")==NULL;
}
