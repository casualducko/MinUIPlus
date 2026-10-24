// The rumble strength setting (VIB_parsePercent, VIB_scale in api.c)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#define LOG_info(...)
#define USERDATA_PATH "/tmp/minui-rumble-test"
#include "gen/VIB_scale.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
int main(void) {
	CHECK(VIB_parsePercent("50\n")==50 && VIB_parsePercent("75%")==75 && VIB_parsePercent("100")==100, "50, 75%, 100");
	CHECK(VIB_parsePercent("0")==0, "0 turns it off");
	CHECK(VIB_parsePercent("101")==100 && VIB_parsePercent("")==100 && VIB_parsePercent(NULL)==100 && VIB_parsePercent("abc")==100 && VIB_parsePercent("-5")==100 && VIB_parsePercent("50 percent")==100, "anything odd: as the game asks (100)");
	mkdir(USERDATA_PATH, 0777);
	char path[600]; snprintf(path, sizeof(path), "%s/rumble", USERDATA_PATH);
	FILE* f = fopen(path, "w"); fputs("50\n", f); fclose(f);
	CHECK(VIB_scale(65535)==32767 && VIB_scale(0)==0 && VIB_scale(1000)==500, "50%: half of what is asked, 0 stays 0");
	unlink(path); rmdir(USERDATA_PATH);
	return fails ? 1 : 0;
}
