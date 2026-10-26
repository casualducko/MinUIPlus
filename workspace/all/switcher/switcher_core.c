#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "switcher_core.h"

int SW_parseRecents(char* text, char paths[][SW_PATH_MAX], int max) {
	int count = 0;
	char* save = NULL;
	for (char* line = strtok_r(text, "\n", &save); line && count<max; line = strtok_r(NULL, "\n", &save)) {
		char* tab = strchr(line, '\t');
		if (tab) *tab = '\0';
		int len = strlen(line);
		while (len>0 && (line[len-1]=='\r' || line[len-1]==' ')) line[--len] = '\0';
		if (!len || len>=SW_PATH_MAX) continue;
		snprintf(paths[count++], SW_PATH_MAX, "%s", line);
	}
	return count;
}
int SW_dropRecent(const char* text, const char* path, char* out, size_t cap) {
	size_t len = 0;
	if (cap) out[0] = '\0';
	size_t plen = strlen(path);
	for (const char* line = text; *line; ) {
		const char* end = strchr(line, '\n');
		size_t n = end ? (size_t)(end - line) : strlen(line);
		int match = n>=plen && !strncmp(line, path, plen) && (n==plen || line[plen]=='\t' || line[plen]=='\r');
		if (!match && n>0) {
			if (len + n + 2 > cap) break;
			memcpy(out+len, line, n);
			len += n;
			out[len++] = '\n';
			out[len] = '\0';
		}
		line += n + (end ? 1 : 0);
	}
	return (int)len;
}
int SW_parseSlot(const char* text) {
	if (!text) return -1;
	while (*text==' ' || *text=='\t' || *text=='\n' || *text=='\r') text++;
	if (*text<'0' || *text>'9') return -1;
	char* end;
	long v = strtol(text, &end, 10);
	if (v<0 || v>9) return -1;
	while (*end==' ' || *end=='\t' || *end=='\n' || *end=='\r') end++;
	return *end ? -1 : (int)v;
}
int SW_pickSlot(int manual_slot, long manual_at, long auto_at) {
	if (manual_slot<0 || manual_slot>8) manual_slot = -1;
	if (auto_at>0 && (manual_slot<0 || auto_at>manual_at)) return 9;
	return manual_slot;
}
void SW_ago(long s, char* out, size_t cap) {
	if (s<60) snprintf(out, cap, "just now");
	else if (s<3600) snprintf(out, cap, "%ld minute%s ago", s/60, s/60==1 ? "" : "s");
	else if (s<86400) snprintf(out, cap, "%ld hour%s ago", s/3600, s/3600==1 ? "" : "s");
	else if (s<86400L*30) snprintf(out, cap, "%ld day%s ago", s/86400, s/86400==1 ? "" : "s");
	else if (s<86400L*365) snprintf(out, cap, "%ld month%s ago", s/(86400L*30), s/(86400L*30)==1 ? "" : "s");
	else snprintf(out, cap, "%ld year%s ago", s/(86400L*365), s/(86400L*365)==1 ? "" : "s");
}
const char* SW_baseName(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash+1 : path;
}
