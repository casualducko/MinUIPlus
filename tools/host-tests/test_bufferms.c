// audio-buffer file parsing (SND_parseBufferMs in api.c)
#include <stdio.h>
#include <string.h>
#include "gen/SND_bufferMs.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
int main(void) {
	CHECK(SND_parseBufferMs("80\n")==80, "80");
	CHECK(SND_parseBufferMs("20")==20 && SND_parseBufferMs("500")==500, "20 and 500 are the limits");
	CHECK(SND_parseBufferMs("19")==0 && SND_parseBufferMs("501")==0, "outside 20..500: default");
	CHECK(SND_parseBufferMs("0")==0, "0 means the default");
	CHECK(SND_parseBufferMs("")==0 && SND_parseBufferMs(NULL)==0, "empty or NULL: default");
	CHECK(SND_parseBufferMs("80 ms")==0, "trailing words: default");
	CHECK(SND_parseBufferMs("-5")==0 && SND_parseBufferMs("abc")==0, "not a number: default");
	CHECK(SND_parseBufferMs("99999999999999")==0, "huge number does not overflow");
	return fails ? 1 : 0;
}
