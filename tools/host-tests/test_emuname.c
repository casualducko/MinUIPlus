// getEmuName() from utils.c: tag extraction and the folder names that used to crash it
#include <stdio.h>
#include <string.h>
#include <strings.h>
#define ROMS_PATH "/storage/roms/Roms"
int prefixMatch(char* pre, char* str) { return (strncasecmp(pre,str,strlen(pre))==0); }
#include "gen/getEmuName.inc"

static int fails = 0;
static void check(const char* in, const char* want) {
	char out[1024];
	getEmuName(in, out);
	int ok = strcmp(out, want)==0;
	if (!ok) fails++;
	printf("%s  %-52s -> \"%s\"\n", ok?"ok  ":"FAIL", in, out);
}
int main(void) {
	check("Super Nintendo (SFC)", "SFC");
	check("/storage/roms/Roms/Super Nintendo (SFC)", "SFC");
	check("/storage/roms/Roms/Super Nintendo (SFC)/Game.sfc", "SFC");
	check("SFC", "SFC");
	check("Game Boy (GB)", "GB");
	// used to dereference NULL: an opening parenthesis without a closing one
	check("Super Nintendo (SFC", "SFC");
	check("/storage/roms/Roms/Super Nintendo (SFC", "SFC");
	return fails!=0;
}
