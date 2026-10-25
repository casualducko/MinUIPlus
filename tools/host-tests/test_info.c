// the System Info formatting (info_core.c)
#include <stdio.h>
#include <string.h>
#include "info_core.c"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static char out[128];
int main(void) {
	Info_bytes(0, out, sizeof(out));                              CHECK(!strcmp(out, "0 B"), "0 bytes");
	Info_bytes(1023, out, sizeof(out));                           CHECK(!strcmp(out, "1023 B"), "just under a KB");
	Info_bytes(1024, out, sizeof(out));                           CHECK(!strcmp(out, "1.0 KB"), "1 KB");
	Info_bytes(3276, out, sizeof(out));                           CHECK(!strcmp(out, "3.2 KB"), "3.2 KB");
	Info_bytes(512ULL*1024*1024, out, sizeof(out));               CHECK(!strcmp(out, "512 MB"), "512 MB (three digits: no decimals)");
	Info_bytes(1288490189ULL, out, sizeof(out));                  CHECK(!strcmp(out, "1.2 GB"), "1.2 GB");
	Info_bytes(127863512064ULL, out, sizeof(out));                CHECK(!strcmp(out, "119.1 GB") || !strcmp(out, "119 GB"), "a 128 GB card");
	Info_bytes(5ULL*1024*1024*1024*1024, out, sizeof(out));       CHECK(!strcmp(out, "5.0 TB"), "5 TB");
	Info_bytes(1000ULL*1024*1024*1024*1024*1024, out, sizeof(out)); CHECK(strstr(out, "TB")!=NULL, "beyond TB stays in TB");
	char tiny[6]; Info_bytes(127863512064ULL, tiny, sizeof(tiny)); CHECK(strlen(tiny)<sizeof(tiny), "a small buffer");

	Info_duration(0, out, sizeof(out));        CHECK(!strcmp(out, "0 s"), "0 s");
	Info_duration(59, out, sizeof(out));       CHECK(!strcmp(out, "59 s"), "59 s");
	Info_duration(60, out, sizeof(out));       CHECK(!strcmp(out, "1 min"), "1 min");
	Info_duration(3599, out, sizeof(out));     CHECK(!strcmp(out, "59 min"), "59 min");
	Info_duration(3600, out, sizeof(out));     CHECK(!strcmp(out, "1 h 00 min"), "1 h");
	Info_duration(11100, out, sizeof(out));    CHECK(!strcmp(out, "3 h 05 min"), "3 h 05 min");
	Info_duration(86400+3*3600+120, out, sizeof(out)); CHECK(!strcmp(out, "1 d 3 h"), "1 d 3 h");
	Info_duration(-5, out, sizeof(out));       CHECK(!strcmp(out, "0 s"), "negative is 0");

	uint64_t total = 0, avail = 0;
	const char* mem = "MemTotal:        2015040 kB\nMemFree:          200000 kB\nMemAvailable:    1200000 kB\nBuffers:           1000 kB\n";
	CHECK(Info_meminfo(mem, &total, &avail) && total==2015040ULL*1024 && avail==1200000ULL*1024, "meminfo");
	CHECK(Info_meminfo("MemTotal: 1000 kB\nMemFree: 400 kB\n", &total, &avail) && avail==400ULL*1024, "no MemAvailable: MemFree");
	CHECK(!Info_meminfo("MemFree: 400 kB\n", &total, &avail), "no MemTotal");
	CHECK(!Info_meminfo("", &total, &avail) && !Info_meminfo(NULL, &total, &avail), "empty and NULL");
	CHECK(!Info_meminfo("MemTotal: lots\nMemAvailable: 5 kB\n", &total, &avail), "a value that is not a number");
	CHECK(!Info_meminfo("MemTotalX: 100 kB\nMemAvailable: 5 kB\n", &total, &avail), "a key that only starts the same");

	Info_temperature("47500\n", out, sizeof(out));  CHECK(!strcmp(out, "47.5 C"), "47.5 C");
	Info_temperature("  -3000", out, sizeof(out));  CHECK(!strcmp(out, "-3.0 C"), "below zero");
	Info_temperature("hot", out, sizeof(out));      CHECK(out[0]=='\0', "not a number");
	Info_temperature("9999999", out, sizeof(out));  CHECK(out[0]=='\0', "not a temperature");
	Info_temperature(NULL, out, sizeof(out));       CHECK(out[0]=='\0', "NULL");

	Info_frequency("1416000\n", out, sizeof(out));  CHECK(!strcmp(out, "1.42 GHz"), "1.42 GHz");
	Info_frequency("816000", out, sizeof(out));     CHECK(!strcmp(out, "816 MHz"), "816 MHz");
	Info_frequency("0", out, sizeof(out));          CHECK(out[0]=='\0', "0 is no frequency");
	Info_frequency("x", out, sizeof(out));          CHECK(out[0]=='\0', "not a number");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
