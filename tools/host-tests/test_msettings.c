// msettings.c: what we do with a settings file that is corrupt or cut short
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include "gen/msettings_types.inc"

static int fails = 0;
#define CHECK(c,m) do { int ok=(c); printf("%s  %s\n", ok?"ok  ":"FAIL", m); if(!ok) fails++; } while(0)
int main(void) {
	Settings s = { .version = 2, .brightness = 99999, .headphones = -5, .speaker = 123456, .jack = 7, .hdmi = -1 };
	SanitizeSettings(&s);
	CHECK(s.brightness==10, "brightness is clamped to 10");
	CHECK(s.headphones==0, "a negative headphone volume becomes 0");
	CHECK(s.speaker==20, "speaker volume is clamped to 20 (never 123456*5 percent)");
	CHECK(s.jack==1 && s.hdmi==1, "flags are 0 or 1");

	Settings sane = { .version = 2, .brightness = 4, .headphones = 6, .speaker = 11, .jack = 0, .hdmi = 0 };
	Settings copy = sane; SanitizeSettings(&copy);
	CHECK(!memcmp(&sane, &copy, sizeof(sane)), "sane settings are left exactly as they were");

	Settings d = DefaultSettings; SanitizeSettings(&d);
	CHECK(!memcmp(&d, &DefaultSettings, sizeof(d)), "the defaults are within range");
	return fails!=0;
}
