// ClockNet_sync (clocknet.c) against mock_time.py: needs CLOCK_URL (set by run.sh)
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdint.h>
static uint64_t getMicroseconds(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec*1000000 + ts.tv_nsec/1000; }
#include "gen/httpdate.inc"
#include "clocknet.c"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static int never(void* ctx, const char* m) { (void)ctx; (void)m; return 0; }
static int always(void* ctx, const char* m) { (void)ctx; (void)m; return 1; }
static int ticks = 0;
static char last_message[200];
static int cancel_after(void* ctx, const char* m) { (void)ctx; snprintf(last_message, sizeof(last_message), "%s", m); return ++ticks>=3; }
int main(void) {
	char url[256];
	snprintf(url, sizeof(url), "%s", getenv("CLOCK_URL") ? getenv("CLOCK_URL") : "");
	if (!url[0]) { printf("CLOCK_URL not set\n"); return 1; }
	time_t now = 0;
	const char* error = "";
	CHECK(ClockNet_sync(&now, &error, never, NULL)==0 && now==1792902600, "the time comes from the Date header");

	setenv("CLOCK_URL", "http://127.0.0.1:1/x", 1); // nothing listens there
	now = 0;
	CHECK(ClockNet_sync(&now, &error, never, NULL)==-1 && now==0 && strstr(error, "Couldn't get the time"), "a dead server: a message, no time");

	setenv("CLOCK_URL", url, 1);
	CHECK(ClockNet_sync(&now, &error, always, NULL)==-1 && !strcmp(error, "Cancelled."), "B while asking: cancelled");
	ticks = 0;
	CHECK(ClockNet_sync(&now, &error, cancel_after, NULL)==-1 || ticks>0, "the tick is called while waiting");
	CHECK(strlen(last_message)>0 || ticks==0, "the tick gets a message");

	// no default route: Wi-Fi is switched on first (a fake wifictl that never brings it up)
	FILE* f = fopen("clocknet-route.txt", "w");
	fprintf(f, "Iface\tDestination\tGateway\nwlan0\t0001A8C0\t00000000\n");
	fclose(f);
	setenv("CLOCK_ROUTE_FILE", "clocknet-route.txt", 1);
	setenv("CLOCK_WIFICTL", "/usr/bin/true", 1);
	setenv("CLOCK_WIFI_TIMEOUT_MS", "300", 1);
	CHECK(ClockNet_sync(&now, &error, never, NULL)==-1 && strstr(error, "No network"), "no route and Wi-Fi never comes up: a message after the timeout");
	CHECK(ClockNet_sync(&now, &error, always, NULL)==-1 && !strcmp(error, "Cancelled."), "B while waiting for Wi-Fi: cancelled");
	// a default route appears: straight to the time
	f = fopen("clocknet-route.txt", "w");
	fprintf(f, "Iface\tDestination\tGateway\nwlan0\t00000000\t0101A8C0\n");
	fclose(f);
	CHECK(ClockNet_sync(&now, &error, never, NULL)==0 && now==1792902600, "with a route: the time");
	remove("clocknet-route.txt");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
