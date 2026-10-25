// the HTTP Date header parser (HTTPDate_parse in utils.c)
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "gen/httpdate.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
int main(void) {
	// 2026-10-25 04:30:00 UTC
	time_t want = 1792902600;
	CHECK(HTTPDate_parse("HTTP/1.1 204 No Content\r\nDate: Sun, 25 Oct 2026 04:30:00 GMT\r\nContent-Length: 0\r\n\r\n")==want, "a normal response");
	CHECK(HTTPDate_parse("date: Sun, 25 Oct 2026 04:30:00 GMT\n")==want, "lowercase name, bare newline");
	CHECK(HTTPDate_parse("Server: x\nDATE:   Sun, 25 Oct 2026 04:30:00 GMT\r\n")==want, "capitals, extra spaces");
	CHECK(HTTPDate_parse("Date: 25 Oct 2026 04:30:00 GMT")==want, "no weekday, no line end");
	CHECK(HTTPDate_parse("Date: Sat, 01 Jan 2028 00:00:00 GMT\r\n")==1830297600, "another date (a leap year follows)");
	CHECK(HTTPDate_parse("Date: Wed, 29 Feb 2028 23:59:59 GMT\r\n")==1835481599, "29 February");
	CHECK(HTTPDate_parse("Date: Sun, 25 Oct 2026 04:30:60 GMT\r\n")==want+59, "a leap second is the last second of the minute");
	CHECK(HTTPDate_parse(NULL)==0 && HTTPDate_parse("")==0, "nothing");
	CHECK(HTTPDate_parse("HTTP/1.1 200 OK\r\nServer: x\r\n\r\n")==0, "no Date header");
	CHECK(HTTPDate_parse("Date: garbage\r\n")==0, "garbage");
	CHECK(HTTPDate_parse("Date: Sun, 25 Foo 2026 04:30:00 GMT\r\n")==0, "a month that is not one");
	CHECK(HTTPDate_parse("Date: Sun, 32 Oct 2026 04:30:00 GMT\r\n")==0 && HTTPDate_parse("Date: Sun, 25 Oct 2026 24:30:00 GMT\r\n")==0 && HTTPDate_parse("Date: Sun, 25 Oct 2026 04:61:00 GMT\r\n")==0, "out of range fields");
	CHECK(HTTPDate_parse("Date: Thu, 01 Jan 1970 00:00:00 GMT\r\n")==0, "1970 is no date (a clock that never started)");
	CHECK(HTTPDate_parse("Date: Mon, 25 Oct 2101 04:30:00 GMT\r\n")==0, "far in the future is none");
	CHECK(HTTPDate_parse("Date: Sun, 25 Oct 2026 04:30:00 GMT\r\nDate: Mon, 26 Oct 2026 04:30:00 GMT\r\n")==want, "the first Date line is used");
	CHECK(HTTPDate_parse("Expires: Sun, 25 Oct 2026 04:30:00 GMT\r\n")==0, "other headers with a date are not it");
	CHECK(HTTPDate_parse("Date: bad\r\nDate: Sun, 25 Oct 2026 04:30:00 GMT\r\n")==0, "a bad first line means none (nothing is guessed)");
	char longline[600]; memset(longline, 'x', sizeof(longline)-1); longline[sizeof(longline)-1] = 0;
	memcpy(longline, "Date: ", 6);
	CHECK(HTTPDate_parse(longline)==0, "a very long line is safe");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
