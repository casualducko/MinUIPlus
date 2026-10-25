// looks_like_html / file_bytes (romulus.c): a web page where a game should be
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "gen/romulus_html.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static void put(const char* path, const void* data, size_t n) { FILE* f = fopen(path, "wb"); if (n) fwrite(data, 1, n, f); fclose(f); }
int main(void) {
	const char* p = "romulus-html-test.bin";
	put(p, "<!DOCTYPE html><html><body>Please log in</body></html>", 55);
	CHECK(looks_like_html(p), "a doctype page");
	put(p, "  \r\n\t<HTML lang=en>", 18);
	CHECK(looks_like_html(p), "whitespace first, capitals");
	put(p, "\xEF\xBB\xBF<html>", 9);
	CHECK(looks_like_html(p), "a byte order mark first");
	put(p, "<?xml version=\"1.0\"?><error/>", 29);
	CHECK(looks_like_html(p), "an XML error");
	put(p, "<body>x</body>", 14);
	CHECK(looks_like_html(p), "a bare body");
	put(p, "NES\x1A\x02\x01 real rom bytes", 21);
	CHECK(!looks_like_html(p), "a ROM header is not a page");
	put(p, "<not html but a game that starts with a bracket", 47);
	CHECK(!looks_like_html(p), "a file that merely starts with < is not a page");
	put(p, "PK\x03\x04zip", 7);
	CHECK(!looks_like_html(p), "a zip");
	put(p, "", 0);
	CHECK(!looks_like_html(p) && file_bytes(p)==0, "an empty file: not html, zero bytes (caught separately)");
	CHECK(!looks_like_html("no-such-file-here") && file_bytes("no-such-file-here")<0, "a file that isn't there");
	remove(p);
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
