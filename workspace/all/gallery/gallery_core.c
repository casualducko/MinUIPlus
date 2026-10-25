#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "gallery_core.h"

static int stampAt(const char* s) { // s points at YYYYMMDD-HHMMSS and an optional -N, then the end
	for (int i=0; i<8; i++) if (!isdigit((unsigned char)s[i])) return 0;
	if (s[8]!='-') return 0;
	for (int i=9; i<15; i++) if (!isdigit((unsigned char)s[i])) return 0;
	if (s[15]=='\0') return 1;
	if (s[15]!='-' || !isdigit((unsigned char)s[16])) return 0;
	for (int i=16; s[i]; i++) if (!isdigit((unsigned char)s[i])) return 0;
	return 1;
}
static void stripExtension(char* name) {
	char* dot = strrchr(name, '.');
	if (dot && (!strcasecmp(dot, ".png") || !strcasecmp(dot, ".bmp"))) *dot = '\0';
}
int Shots_parse(const char* file, char* game, size_t gsize, char* when, size_t wsize) {
	char name[512];
	snprintf(name, sizeof(name), "%s", file);
	stripExtension(name);
	if (wsize) when[0] = '\0';
	char* space = strrchr(name, ' ');
	if (space && stampAt(space+1)) {
		const char* s = space + 1;
		if (wsize) snprintf(when, wsize, "%.4s-%.2s-%.2s %.2s:%.2s:%.2s", s, s+4, s+6, s+9, s+11, s+13);
		while (space>name && space[-1]==' ') space--;
		*space = '\0';
		snprintf(game, gsize, "%s", name);
		return 1;
	}
	snprintf(game, gsize, "%s", name);
	return 0;
}
static const char* stampOf(const char* file, char* buf, size_t size) { // the stamp part of a name, or "" if none
	char name[512];
	snprintf(name, sizeof(name), "%s", file);
	stripExtension(name);
	char* space = strrchr(name, ' ');
	if (space && stampAt(space+1)) snprintf(buf, size, "%s", space+1);
	else buf[0] = '\0';
	return buf;
}
int Shots_compare(const char* a, const char* b) {
	char sa[32], sb[32];
	stampOf(a, sa, sizeof(sa));
	stampOf(b, sb, sizeof(sb));
	if (sa[0] && sb[0]) {
		// compare YYYYMMDD-HHMMSS first, then the -N (a number, so -10 follows -9)
		int c = strncmp(sa, sb, 15);
		if (c) return -c; // newer (greater) first
		int na = sa[15]=='-' ? atoi(sa+16) : 1, nb = sb[15]=='-' ? atoi(sb+16) : 1;
		if (na!=nb) return nb - na;
		return strcmp(a, b);
	}
	if (sa[0]) return -1;
	if (sb[0]) return 1;
	return strcmp(a, b);
}
int Shots_isImage(const char* file) {
	if (!file || file[0]=='.') return 0;
	const char* dot = strrchr(file, '.');
	if (!dot) return 0;
	return !strcasecmp(dot, ".png") || !strcasecmp(dot, ".bmp");
}
