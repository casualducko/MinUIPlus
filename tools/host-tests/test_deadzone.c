// stick-deadzone file parsing (PAD_parseDeadzone in api.c)
#include <stdio.h>
#include <string.h>
#define STICK_DEADZONE_DEFAULT 40
#include "gen/PAD_deadzone.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
int main(void) {
	CHECK(PAD_parseDeadzone("30\n")==30, "30");
	CHECK(PAD_parseDeadzone("25%")==25, "25%");
	CHECK(PAD_parseDeadzone(" 30")==40, "leading space: default");
	CHECK(PAD_parseDeadzone("")==40, "empty: default");
	CHECK(PAD_parseDeadzone(NULL)==40, "NULL: default");
	CHECK(PAD_parseDeadzone("5")==40, "too small (stick would never rest): default");
	CHECK(PAD_parseDeadzone("95")==40, "too large (stick could never press): default");
	CHECK(PAD_parseDeadzone("10")==10 && PAD_parseDeadzone("90")==90, "10 and 90 are the limits");
	CHECK(PAD_parseDeadzone("30 percent")==40, "trailing words: default");
	CHECK(PAD_parseDeadzone("99999999999999")==40, "huge number does not overflow");
	CHECK(PAD_parseDeadzone("abc")==40, "not a number: default");
	return fails ? 1 : 0;
}
