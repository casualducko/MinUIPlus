#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "titleart_core.h"

static int ta_isExt(const char* s) { // 1 to 4 letters or digits
	int n = 0;
	for (; s[n]; n++) if (!isalnum((unsigned char)s[n])) return 0;
	return n>=1 && n<=4;
}
void TA_title(const char* name, char* out, int size) {
	char t[512];
	snprintf(t, sizeof(t), "%s", name);
	char* cut = strstr(t, " (");
	char* cut2 = strstr(t, " [");
	if (!cut || (cut2 && cut2<cut)) cut = cut2;
	if (cut) *cut = '\0'; // the tags, and the extension after them
	else {
		char* dot = strrchr(t, '.');
		if (dot && ta_isExt(dot+1)) *dot = '\0';
	}
	int len = strlen(t);
	while (len>0 && isspace((unsigned char)t[len-1])) t[--len] = '\0';
	char* start = t;
	while (*start && isspace((unsigned char)*start)) start++;
	len = strlen(start);
	static const char* articles[] = {", The", ", A", ", An", NULL};
	for (int i=0; articles[i]; i++) {
		int al = strlen(articles[i]);
		if (len>al && !strcmp(start+len-al, articles[i])) {
			char head[512];
			snprintf(head, sizeof(head), "%.*s", len-al, start);
			snprintf(out, size, "%s %s", articles[i]+2, head);
			return;
		}
	}
	snprintf(out, size, "%s", start);
}
void TA_split(const char* title, char* main, int msize, char* sub, int ssize) {
	static const char* seps[] = {" - ", ": ", NULL};
	for (int i=0; seps[i]; i++) {
		const char* p = strstr(title, seps[i]);
		if (!p) continue;
		const char* rest = p + strlen(seps[i]);
		int mlen = p - title;
		while (mlen>0 && isspace((unsigned char)title[mlen-1])) mlen--;
		while (*rest && isspace((unsigned char)*rest)) rest++;
		if (mlen>0 && *rest) {
			snprintf(main, msize, "%.*s", mlen, title);
			snprintf(sub, ssize, "%s", rest);
			return;
		}
	}
	snprintf(main, msize, "%s", title);
	sub[0] = '\0';
}
static void ta_upper(char* s) { for (; *s; s++) *s = toupper((unsigned char)*s); }

#define TA_MAX_WORDS 32
static int ta_words(const char* text, char words[][TA_LINE_MAX]) {
	int n = 0;
	const char* p = text;
	while (*p && n<TA_MAX_WORDS) {
		while (*p==' ' || *p=='\t') p++;
		if (!*p) break;
		int len = 0;
		while (p[len] && p[len]!=' ' && p[len]!='\t') len++;
		snprintf(words[n], TA_LINE_MAX, "%.*s", len<TA_LINE_MAX-1 ? len : TA_LINE_MAX-1, p);
		n++;
		p += len;
	}
	return n;
}
static void ta_join(char words[][TA_LINE_MAX], int from, int to, char* out) { // words from..to-1
	out[0] = '\0';
	int len = 0;
	for (int i=from; i<to; i++) {
		int n = snprintf(out+len, TA_LINE_MAX-len, "%s%s", i>from ? " " : "", words[i]);
		if (n<0 || n>=TA_LINE_MAX-len) { out[TA_LINE_MAX-1] = '\0'; return; }
		len += n;
	}
}
typedef struct { int best; char lines[TA_MAX_LINES][TA_LINE_MAX]; int found; } TA_WrapSearch;
static void ta_wrapRec(char words[][TA_LINE_MAX], int count, int i, int k, char cur[][TA_LINE_MAX], int used, TA_WrapSearch* s) {
	if (k==1) {
		ta_join(words, i, count, cur[used]);
		int score = 0;
		for (int l=0; l<=used; l++) { int len = strlen(cur[l]); if (len>score) score = len; }
		if (!s->found || score<s->best) {
			s->found = 1;
			s->best = score;
			for (int l=0; l<=used; l++) memcpy(s->lines[l], cur[l], TA_LINE_MAX);
		}
		return;
	}
	for (int j=i+1; j<=count-k+1; j++) {
		ta_join(words, i, j, cur[used]);
		ta_wrapRec(words, count, j, k-1, cur, used+1, s);
	}
}
int TA_wrapWords(const char* text, int n, char lines[][TA_LINE_MAX]) {
	char words[TA_MAX_WORDS][TA_LINE_MAX];
	int count = ta_words(text, words);
	if (n<1 || n>TA_MAX_LINES || n>count) return 0;
	TA_WrapSearch s = {0};
	char cur[TA_MAX_LINES][TA_LINE_MAX];
	ta_wrapRec(words, count, 0, n, cur, 0, &s);
	if (!s.found) return 0;
	for (int l=0; l<n; l++) memcpy(lines[l], s.lines[l], TA_LINE_MAX);
	return n;
}
int TA_wrapPlain(const char* text, int width, int max_lines, char lines[][TA_LINE_MAX]) {
	char words[TA_MAX_WORDS][TA_LINE_MAX];
	int count = ta_words(text, words);
	int n = 0;
	char cur[TA_LINE_MAX] = "";
	char all[TA_MAX_LINES+8][TA_LINE_MAX];
	for (int i=0; i<count; i++) {
		int add = (cur[0] ? 1 : 0) + (int)strlen(words[i]);
		if ((int)strlen(cur) + add <= width) {
			if (cur[0]) strcat(cur, " ");
			strcat(cur, words[i]);
		}
		else {
			if (cur[0] && n<TA_MAX_LINES+7) snprintf(all[n++], TA_LINE_MAX, "%s", cur);
			snprintf(cur, sizeof(cur), "%s", words[i]);
		}
	}
	if (cur[0] && n<TA_MAX_LINES+7) snprintf(all[n++], TA_LINE_MAX, "%s", cur);
	int out = n>max_lines ? max_lines : n;
	for (int i=0; i<out; i++) snprintf(lines[i], TA_LINE_MAX, "%s", all[i]);
	if (n>max_lines && out>0 && width>1) {
		int len = strlen(lines[out-1]);
		if (len>width-1) len = width-1;
		lines[out-1][len] = '~';
		lines[out-1][len+1] = '\0';
	}
	for (int i=0; i<out; i++) if ((int)strlen(lines[i])>width) lines[i][width] = '\0';
	return out;
}
static int ta_layoutTry(char words[][TA_LINE_MAX], int count, int max_cols, int max_rows, float (*aspect)(const char*, void*), void* ctx, TA_Layout* best, int* have) {
	*have = 0;
	char joined[TA_LINE_MAX*4];
	ta_join(words, 0, count, joined);
	for (int n=1; n<=TA_MAX_LINES; n++) {
		char lines[TA_MAX_LINES][TA_LINE_MAX];
		if (!TA_wrapWords(joined, n, lines)) continue;
		float asp = 0;
		int ok = 1;
		for (int l=0; l<n; l++) {
			float a = aspect(lines[l], ctx);
			if (a<=0) { ok = 0; break; }
			if (a>asp) asp = a;
		}
		if (!ok) continue;
		float h_by_w = max_cols / (2.0f * asp);
		float h_by_h = (float)(max_rows - (n-1)) / n;
		float hf = h_by_w < h_by_h ? h_by_w : h_by_h;
		if (hf>11) hf = 11;
		int h = (int)hf; // truncates, like the prototype's int()
		if (!*have || h>best->h) {
			*have = 1;
			best->h = h;
			best->count = n;
			for (int l=0; l<n; l++) memcpy(best->lines[l], lines[l], TA_LINE_MAX);
		}
	}
	return *have;
}
int TA_layout(const char* text, int max_cols, int max_rows, float (*aspect)(const char*, void*), void* ctx, TA_Layout* out) {
	char up[TA_LINE_MAX*2];
	snprintf(up, sizeof(up), "%s", text[0] ? text : "?");
	ta_upper(up);
	char words[TA_MAX_WORDS][TA_LINE_MAX];
	int count = ta_words(up, words);
	if (!count) { snprintf(words[0], TA_LINE_MAX, "?"); count = 1; }
	for (;;) {
		int have;
		TA_Layout best;
		memset(&best, 0, sizeof(best));
		ta_layoutTry(words, count, max_cols, max_rows, aspect, ctx, &best, &have);
		if (have && best.h>=4) { *out = best; return 1; }
		if (count==1) {
			int len = strlen(words[0]);
			if (len<=6) { if (have) { *out = best; return 1; } return 0; }
			words[0][len-2] = '\0'; // one very long word: cut it
		}
		else count--;                // too long: drop the last word
	}
}
void TA_pack(const uint8_t* px, int w, int rows, char* out) {
	for (int y=0; y<rows; y++) {
		for (int x=0; x<w; x++) {
			int t = px[(2*y)*w + x] >= 108; // > 0.42 of 255
			int b = px[(2*y+1)*w + x] >= 108;
			out[y*w + x] = (t && b) ? 'H' : t ? '\'' : b ? '.' : ' ';
		}
	}
}
int TA_badge(const char* folder, char* out, int size) {
	char name[128];
	snprintf(name, sizeof(name), "%s", folder);
	char tag[64] = "";
	char* paren = strstr(name, " (");
	if (paren) {
		char* end = strchr(paren, ')');
		if (end) snprintf(tag, sizeof(tag), "%.*s", (int)(end-paren-2), paren+2);
		*paren = '\0';
	}
	const char* use = (strlen(name)>14 && tag[0]) ? tag : name;
	snprintf(out, size, "%s", use);
	ta_upper(out);
	return out[0] != '\0';
}
