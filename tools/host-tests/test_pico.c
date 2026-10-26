// The Pico Search tool's logic (workspace/all/pico/bbs.c): the listing and thread pages, ranking, file names and the curl job against mock_bbs.py.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "bbs.h"

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("ok    %s\n", msg); else { printf("FAIL  %s\n", msg); fails++; } } while (0)

static char* slurp(const char* dir, const char* name, size_t* len) {
	char path[512];
	snprintf(path, sizeof(path), "%s/fixtures/%s", dir, name);
	FILE* f = fopen(path, "rb");
	if (!f) { printf("FAIL  can't open %s\n", path); exit(1); }
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	char* b = malloc(n+1);
	if (fread(b, 1, n, f)!=(size_t)n) exit(1);
	b[n] = '\0'; fclose(f);
	*len = n;
	return b;
}

int main(int argc, char** argv) {
	const char* dir = argc>1 ? argv[1] : ".";
	size_t len;
	char* page = slurp(dir, "bbs_search.html", &len);
	BbsCart c[64];
	int n = bbs_parse_search(page, len, c, 64);
	CHECK(n==5, "the listing has 5 carts");
	CHECK(n==5 && c[0].tid==2145 && !strcmp(c[0].title, "Celeste") && !strcmp(c[0].author, "noel") && c[0].stars==2068 && !strcmp(c[0].date, "2015-07-21 06:06:51"), "title, author, date, stars and thread of a row");
	CHECK(n==5 && !strcmp(c[1].title, "celeste classic 2") && c[1].tid==41282, "a second row");
	CHECK(n==5 && !strcmp(c[3].tags, "platform, dash, parkour") && c[3].stars==168, "tags are joined, the trailing comma ignored");
	CHECK(n==5 && !strncmp(c[3].title, "\xe2\x97\x86", 3), "a title with a non-ASCII character is kept");
	CHECK(n==5 && c[4].tid==41010 && !strcmp(c[4].author, "petthepetra") && !strcmp(c[4].tags, "celeste, mod, platformer"), "the last row");
	CHECK(bbs_parse_search(page, len, c, 2)==2, "no more than max");
	CHECK(bbs_parse_search("<html>nothing here</html>", 25, c, 64)==-1, "a page with no listing: -1");
	const char* empty = "<script>pdat=[\n\t];</script>";
	CHECK(bbs_parse_search(empty, strlen(empty), c, 64)==0, "an empty listing: 0");
	const char* odd = "pdat=[ ['1', 7, `Tom \\`Q\\` &amp; Jerry &#233;`,\"t\",1,1,\"2020-01-01 00:00:00\",1,\"a&amp;b\",\"x\",1,\"y\",5,1,0,7,2,'0',[\"a\",\"b\"],0,1,,``,``] ];";
	CHECK(bbs_parse_search(odd, strlen(odd), c, 4)==1 && !strcmp(c[0].title, "Tom `Q` & Jerry \xc3\xa9") && !strcmp(c[0].author, "a&b"), "escapes and entities in a title");
	const char* torn = "pdat=[ ['1', 7, `Cut off";
	CHECK(bbs_parse_search(torn, strlen(torn), c, 4)==0, "a page cut in the middle of a row: no row");
	free(page);

	page = slurp(dir, "bbs_thread.html", &len);
	char path[256];
	CHECK(bbs_parse_cart_path(page, len, path, sizeof(path))==0 && !strcmp(path, "/bbs/cposts/1/15133.p8.png"), "the cart link of a thread (not the player's script)");
	free(page);
	const char* abs = "<a href=\"https://www.lexaloffle.com/bbs/cposts/ce/celeste.p8.png\">Cart</a>";
	CHECK(bbs_parse_cart_path(abs, strlen(abs), path, sizeof(path))==0 && !strcmp(path, "/bbs/cposts/ce/celeste.p8.png"), "an absolute link and a named cart");
	const char* evil = "<a href=\"/bbs/cposts/1/a b;rm.p8.png\">x</a><a href=\"/bbs/cposts/1/../../x.p8.png?x\">y</a>";
	CHECK(bbs_parse_cart_path(evil, strlen(evil), path, sizeof(path))!=0, "links with odd characters are not followed");
	const char* none = "<a href=\"/bbs/cposts/1/15133.p8.rom\">x</a><a href=\"/other/x.p8.png\">y</a>";
	CHECK(bbs_parse_cart_path(none, strlen(none), path, sizeof(path))!=0, "other links are not carts");

	// urls
	char url[1024];
	setenv("PICO_BBS_BASE", "http://127.0.0.1:1234", 1);
	bbs_search_url(url, sizeof(url), "super mario & co", 2);
	CHECK(!strcmp(url, "http://127.0.0.1:1234/bbs/?cat=7&carts_tab=1&page=2&search=super%20mario%20%26%20co"), "the search url is encoded");
	bbs_thread_url(url, sizeof(url), 2145);
	CHECK(!strcmp(url, "http://127.0.0.1:1234/bbs/?tid=2145"), "the thread url");
	bbs_cart_url(url, sizeof(url), "/bbs/cposts/1/15133.p8.png");
	CHECK(!strcmp(url, "http://127.0.0.1:1234/bbs/cposts/1/15133.p8.png"), "the cart url");
	unsetenv("PICO_BBS_BASE");
	bbs_search_url(url, sizeof(url), "", 1);
	CHECK(!strcmp(url, "https://www.lexaloffle.com/bbs/?cat=7&carts_tab=1&page=1&search="), "the default is https and the real site");

	// ranking
	BbsCart a = {0}, b = {0}, d = {0}, e = {0};
	snprintf(a.title, sizeof(a.title), "The Lost Night Release"); snprintf(a.tags, sizeof(a.tags), "rpg, earthbound, mario"); a.stars = 300;
	snprintf(b.title, sizeof(b.title), "Super Mario Bros."); b.stars = 150;
	snprintf(d.title, sizeof(d.title), "Mario Kart Clone"); snprintf(d.tags, sizeof(d.tags), "racing"); d.stars = 20;
	snprintf(e.title, sizeof(e.title), "Mario"); snprintf(e.tags, sizeof(e.tags), "nes"); e.stars = 10;
	CHECK(bbs_match_tier(&b, "mario")==3 && bbs_match_tier(&a, "mario")==1 && bbs_match_tier(&a, "zelda")==0, "tiers: in the title, in the tags, neither");
	CHECK(bbs_match_tier(&b, "bros super")==2 && bbs_match_tier(&b, "SUPER mario")==3 && bbs_match_tier(&b, "  ")==0, "every word in the title; any case; blank is nothing");
	BbsCart list[4] = { a, d, b, e };
	bbs_sort(list, 4, "mario", BBS_SORT_BEST);
	CHECK(!strcmp(list[0].title, "Super Mario Bros.") && !strcmp(list[1].title, "Mario Kart Clone") && !strcmp(list[2].title, "Mario") && !strcmp(list[3].title, "The Lost Night Release"), "best match: title hits first, by stars, tag-only last");
	bbs_sort(list, 4, "mario", BBS_SORT_STARS);
	CHECK(list[0].stars==300 && list[3].stars==10, "most stars first");
	snprintf(list[0].date, 24, "2020-01-01"); snprintf(list[1].date, 24, "2024-05-05"); snprintf(list[2].date, 24, "2021-02-02"); snprintf(list[3].date, 24, "2024-05-05");
	bbs_sort(list, 4, "mario", BBS_SORT_NEWEST);
	CHECK(!strcmp(list[0].date, "2024-05-05") && !strcmp(list[3].date, "2020-01-01"), "newest first");

	// file names
	char name[256];
	bbs_file_name("Celeste Classic", NULL, name, sizeof(name));
	CHECK(!strcmp(name, "Celeste Classic.p8.png"), "a plain name");
	bbs_file_name("  a/b\\c:d*e?f\"g<h>i|j  ", NULL, name, sizeof(name));
	CHECK(!strcmp(name, "a b c d e f g h i j.p8.png"), "characters a FAT card dislikes become spaces");
	bbs_file_name("Pico de Pon", "(41282)", name, sizeof(name));
	CHECK(!strcmp(name, "Pico de Pon (41282).p8.png"), "a suffix goes before the extension");
	bbs_file_name("...", NULL, name, sizeof(name));
	CHECK(!strcmp(name, "cart.p8.png"), "nothing left: cart");
	char longt[400];
	memset(longt, 'x', sizeof(longt)); longt[399] = '\0';
	bbs_file_name(longt, NULL, name, sizeof(name));
	CHECK(strlen(name)==80+7, "a long title is cut");
	char jp[400] = "";
	for (int i=0; i<60; i++) strcat(jp, "\xe3\x81\x82");
	bbs_file_name(jp, NULL, name, sizeof(name));
	CHECK(strlen(name)>7 && ((strlen(name)-7)%3)==0, "a long Japanese title is cut between characters");

	// the curl job against the mock server
	const char* port = getenv("PICO_PORT");
	if (port) {
		char base[64];
		snprintf(base, sizeof(base), "http://127.0.0.1:%s", port);
		setenv("PICO_BBS_BASE", base, 1);
		const char* out = "/tmp/pico-test-out";
		BbsJob job;
		bbs_search_url(url, sizeof(url), "celeste", 1);
		CHECK(bbs_job_start(&job, url, out, 0)==0, "a job starts");
		for (int i=0; i<200 && !bbs_job_poll(&job); i++) usleep(50000);
		CHECK(job.done && !bbs_job_error(&job), "the search page comes back");
		size_t flen = 0;
		FILE* f = fopen(out, "rb");
		char* body = NULL;
		if (f) { fseek(f, 0, SEEK_END); flen = ftell(f); fseek(f, 0, SEEK_SET); body = malloc(flen+1); if (fread(body, 1, flen, f)!=flen) flen = 0; fclose(f); }
		CHECK(body && bbs_parse_search(body, flen, c, 64)==5, "...and parses");
		free(body);
		bbs_job_cleanup(&job);
		bbs_thread_url(url, sizeof(url), 2145);
		bbs_job_start(&job, url, out, 0);
		for (int i=0; i<200 && !bbs_job_poll(&job); i++) usleep(50000);
		f = fopen(out, "rb"); body = NULL; flen = 0;
		if (f) { fseek(f, 0, SEEK_END); flen = ftell(f); fseek(f, 0, SEEK_SET); body = malloc(flen+1); if (fread(body, 1, flen, f)!=flen) flen = 0; fclose(f); }
		CHECK(body && bbs_parse_cart_path(body, flen, path, sizeof(path))==0, "a thread page gives its cart path");
		free(body);
		bbs_job_cleanup(&job);
		bbs_cart_url(url, sizeof(url), "/bbs/cposts/1/15133.p8.png");
		bbs_job_start(&job, url, out, 1);
		for (int i=0; i<200 && !bbs_job_poll(&job); i++) usleep(50000);
		CHECK(job.done && !bbs_job_error(&job) && bbs_is_png(out) && bbs_job_bytes(&job)>1024, "the cart downloads and is a PNG");
		bbs_job_cleanup(&job);
		bbs_thread_url(url, sizeof(url), 404);
		bbs_job_start(&job, url, out, 0);
		for (int i=0; i<200 && !bbs_job_poll(&job); i++) usleep(50000);
		CHECK(bbs_job_error(&job) && !strcmp(bbs_job_error(&job), "Not found on lexaloffle.com"), "a 404 is reported");
		bbs_job_cleanup(&job);
		bbs_job_start(&job, url, out, 0);
		bbs_job_cancel(&job);
		CHECK(bbs_job_error(&job) && !strcmp(bbs_job_error(&job), "Cancelled"), "a job can be cancelled");
		bbs_job_cleanup(&job);
		FILE* tiny = fopen(out, "wb"); fputs("<html>login page</html>", tiny); fclose(tiny);
		CHECK(!bbs_is_png(out), "a web page is not a cart");
		unlink(out);
	}
	printf("%s\n", fails ? "SOME FAILED" : "all passed");
	return fails ? 1 : 0;
}
