// PWR_parseSleepSeconds() from api.c: the autosleep timeout file
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#define LOG_info(...)
#define USERDATA_PATH "/nonexistent"
#include "gen/PWR_sleep.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
int main(void) {
	CHECK(PWR_parseSleepSeconds("300")==300, "300 seconds");
	CHECK(PWR_parseSleepSeconds("300\n")==300 && PWR_parseSleepSeconds("300\r\n")==300 && PWR_parseSleepSeconds("  45 ")==45, "newline, CRLF and spaces are fine");
	CHECK(PWR_parseSleepSeconds("0")==0 && PWR_parseSleepSeconds("0\n")==0, "0 means never");
	CHECK(PWR_parseSleepSeconds("10")==10 && PWR_parseSleepSeconds("86400")==86400, "10 and 86400 are the limits");
	CHECK(PWR_parseSleepSeconds("5")==SLEEP_TIMEOUT_DEFAULT && PWR_parseSleepSeconds("1")==SLEEP_TIMEOUT_DEFAULT, "under 10 seconds is ignored (no sleeping at once)");
	CHECK(PWR_parseSleepSeconds("86401")==SLEEP_TIMEOUT_DEFAULT && PWR_parseSleepSeconds("99999999999999999999")==SLEEP_TIMEOUT_DEFAULT, "too big, no overflow");
	CHECK(PWR_parseSleepSeconds("")==SLEEP_TIMEOUT_DEFAULT && PWR_parseSleepSeconds(NULL)==SLEEP_TIMEOUT_DEFAULT, "empty or missing: default");
	CHECK(PWR_parseSleepSeconds("-5")==SLEEP_TIMEOUT_DEFAULT && PWR_parseSleepSeconds("abc")==SLEEP_TIMEOUT_DEFAULT && PWR_parseSleepSeconds("2 minutes")==SLEEP_TIMEOUT_DEFAULT && PWR_parseSleepSeconds("1e3")==SLEEP_TIMEOUT_DEFAULT, "junk: default");
	CHECK(SLEEP_TIMEOUT_DEFAULT==120, "default is 2 minutes");
	CHECK(PWR_sleepDelay()==120000 && PWR_sleepDelay()==120000, "no file: 120 s, read once");
	printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
	return fails != 0;
}
