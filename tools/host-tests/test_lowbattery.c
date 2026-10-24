// The low-battery shutdown decision (PWR_parseLowBattery, PWR_lowBatteryCheck in api.c)
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
	CHECK(PWR_parseLowBattery("5\n")==5 && PWR_parseLowBattery("8%")==8 && PWR_parseLowBattery("10")==10, "5, 8%, 10");
	CHECK(PWR_parseLowBattery("0")==0, "0 turns it off");
	CHECK(PWR_parseLowBattery("20")==20 && PWR_parseLowBattery("21")==LOW_BATTERY_DEFAULT, "20 is the highest allowed (a typo can't make it shut down at half charge)");
	CHECK(PWR_parseLowBattery("")==LOW_BATTERY_DEFAULT && PWR_parseLowBattery(NULL)==LOW_BATTERY_DEFAULT && PWR_parseLowBattery("abc")==LOW_BATTERY_DEFAULT, "empty, NULL, not a number: default");
	CHECK(PWR_parseLowBattery("5 percent")==LOW_BATTERY_DEFAULT && PWR_parseLowBattery("-3")==LOW_BATTERY_DEFAULT, "trailing words or a minus sign: default");

	int s = 0;
	CHECK(!PWR_lowBatteryCheck(4, 0, 5, &s, 3) && !PWR_lowBatteryCheck(4, 0, 5, &s, 3) && PWR_lowBatteryCheck(4, 0, 5, &s, 3), "three low readings in a row: shut down on the third");
	s = 0;
	PWR_lowBatteryCheck(3, 0, 5, &s, 3); PWR_lowBatteryCheck(3, 0, 5, &s, 3);
	CHECK(!PWR_lowBatteryCheck(40, 0, 5, &s, 3) && s==0, "one normal reading in between starts the count again");
	CHECK(!PWR_lowBatteryCheck(3, 0, 5, &s, 3), "...so the next low one is only the first");
	s = 0;
	for (int i=0;i<10;i++) if (PWR_lowBatteryCheck(2, 1, 5, &s, 3)) { s = -99; break; }
	CHECK(s==0, "never while charging, however low");
	s = 0;
	for (int i=0;i<10;i++) if (PWR_lowBatteryCheck(-1, 0, 5, &s, 3)) { s = -99; break; }
	CHECK(s==0, "never when the charge is unknown");
	s = 0;
	for (int i=0;i<10;i++) if (PWR_lowBatteryCheck(1, 0, 0, &s, 3)) { s = -99; break; }
	CHECK(s==0, "never when the limit is 0 (off)");
	s = 0;
	CHECK(!PWR_lowBatteryCheck(6, 0, 5, &s, 1), "6% is above a 5% limit");
	CHECK(PWR_lowBatteryCheck(5, 0, 5, &s, 1), "5% is at the limit");
	return fails ? 1 : 0;
}
