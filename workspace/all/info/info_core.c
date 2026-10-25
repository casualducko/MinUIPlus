#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "info_core.h"

void Info_bytes(uint64_t bytes, char* out, size_t size) {
	static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
	double v = (double)bytes;
	int u = 0;
	while (v>=1024.0 && u<4) { v /= 1024.0; u++; }
	if (u==0) snprintf(out, size, "%llu B", (unsigned long long)bytes);
	else if (v>=100.0) snprintf(out, size, "%.0f %s", v, units[u]);
	else snprintf(out, size, "%.1f %s", v, units[u]);
}
void Info_duration(long seconds, char* out, size_t size) {
	if (seconds<0) seconds = 0;
	long days = seconds / 86400, hours = (seconds % 86400) / 3600, minutes = (seconds % 3600) / 60;
	if (days>0) snprintf(out, size, "%ld d %ld h", days, hours);
	else if (hours>0) snprintf(out, size, "%ld h %02ld min", hours, minutes);
	else if (minutes>0) snprintf(out, size, "%ld min", minutes);
	else snprintf(out, size, "%ld s", seconds);
}
static int meminfoValue(const char* text, const char* key, uint64_t* out) { // "MemTotal:  2048000 kB"
	size_t klen = strlen(key);
	for (const char* p = text; *p; ) {
		if (!strncmp(p, key, klen) && p[klen]==':') {
			const char* q = p + klen + 1;
			while (*q==' ' || *q=='\t') q++;
			if (!isdigit((unsigned char)*q)) return 0;
			*out = strtoull(q, NULL, 10) * 1024; // kB
			return 1;
		}
		const char* eol = strchr(p, '\n');
		if (!eol) break;
		p = eol + 1;
	}
	return 0;
}
int Info_meminfo(const char* text, uint64_t* total, uint64_t* available) {
	uint64_t t = 0, a = 0;
	if (!text || !meminfoValue(text, "MemTotal", &t)) return 0;
	if (!meminfoValue(text, "MemAvailable", &a) && !meminfoValue(text, "MemFree", &a)) return 0; // old kernels have no MemAvailable
	*total = t;
	*available = a;
	return 1;
}
void Info_temperature(const char* text, char* out, size_t size) {
	out[0] = '\0';
	if (!text) return;
	while (*text==' ' || *text=='\t') text++;
	char* end = NULL;
	long milli = strtol(text, &end, 10);
	if (end==text) return;
	if (milli<-50000 || milli>150000) return; // not a temperature
	snprintf(out, size, "%.1f C", milli / 1000.0);
}
void Info_frequency(const char* text, char* out, size_t size) {
	out[0] = '\0';
	if (!text) return;
	while (*text==' ' || *text=='\t') text++;
	char* end = NULL;
	long khz = strtol(text, &end, 10);
	if (end==text || khz<=0) return;
	if (khz>=1000000) snprintf(out, size, "%.2f GHz", khz / 1000000.0);
	else snprintf(out, size, "%ld MHz", khz / 1000);
}
