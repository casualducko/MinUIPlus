// UTF8_next (utils.c): decoding one character at a time
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "gen/utf8.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static int one(const char* s, uint32_t* cp) { return UTF8_next(s, cp); }
int main(void) {
	uint32_t cp;
	CHECK(one("A", &cp)==1 && cp=='A', "ASCII");
	CHECK(one("", &cp)==0, "the end");
	CHECK(one("\xC3\xA9", &cp)==2 && cp==0xE9, "two bytes (e acute)");
	CHECK(one("\xE3\x83\x9D", &cp)==3 && cp==0x30DD, "three bytes (katakana PO)");
	CHECK(one("\xE7\xA5\x9E", &cp)==3 && cp==0x795E, "three bytes (kanji)");
	CHECK(one("\xF0\x9F\x8E\xAE", &cp)==4 && cp==0x1F3AE, "four bytes (a game controller emoji)");
	CHECK(one("\x80", &cp)==1 && cp==0xFFFD, "a stray continuation byte");
	CHECK(one("\xE3\x83", &cp)==1 && cp==0xFFFD, "a cut sequence at the end");
	CHECK(one("\xE3\x41\x42", &cp)==1 && cp==0xFFFD, "a lead byte followed by ASCII");
	CHECK(one("\xC0\x80", &cp)==1 && cp==0xFFFD, "an overlong NUL");
	CHECK(one("\xE0\x80\x80", &cp)==1 && cp==0xFFFD, "an overlong three byte form");
	CHECK(one("\xED\xA0\x80", &cp)==1 && cp==0xFFFD, "a surrogate");
	CHECK(one("\xF4\x90\x80\x80", &cp)==1 && cp==0xFFFD, "above U+10FFFF");
	CHECK(one("\xFF", &cp)==1 && cp==0xFFFD, "0xFF");
	const char* s = "A\xE3\x83\x9D\x80Z";
	int total = 0, steps = 0;
	for (const char* p = s; *p; ) { int n = UTF8_next(p, &cp); if (n<=0) break; p += n; total += n; steps++; }
	CHECK(total==(int)strlen(s) && steps==4, "walking a string with a bad byte in it advances every time");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
