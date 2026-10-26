// Client side of the Lexaloffle BBS for the Pico Search tool: the search and thread pages are HTML with
// the listing inside a JavaScript array (`pdat=[...]`); a cart is a .p8.png file linked from its thread.
// No SDL in here so it can be tested on a computer.
#ifndef PICO_BBS_H
#define PICO_BBS_H
#include <stddef.h>
#include <sys/types.h>

#define BBS_PAGE_SIZE 30 // carts the BBS lists on one search page

typedef struct {
	char pid[16];		// the post the listing row is for
	long tid;			// the thread (its page links the cart)
	char title[160];
	char author[48];
	char date[24];		// "2021-02-27 17:33:02"
	int stars;
	char tags[128];		// "rpg, earthbound, shooter"
} BbsCart;

// The base is https://www.lexaloffle.com unless PICO_BBS_BASE says otherwise (tests). Both build into out.
void bbs_search_url(char* out, size_t cap, const char* query, int page); // page from 1
void bbs_thread_url(char* out, size_t cap, long tid);
void bbs_cart_url(char* out, size_t cap, const char* cart_path); // "/bbs/cposts/1/15133.p8.png"
void bbs_urlencode(const char* in, char* out, size_t cap);

// Parses the carts out of a search page. Returns how many were put in out (up to max), -1 if the page has no listing.
int bbs_parse_search(const char* html, size_t len, BbsCart* out, int max);
// Finds the first cart file link in a thread page ("/bbs/cposts/1/15133.p8.png"). 0 and the path, or -1.
int bbs_parse_cart_path(const char* html, size_t len, char* path, size_t cap);

// Ranking for a query: 3 the whole query is in the title, 2 every word is, 1 every word is in the title or the tags, 0 the rest.
int bbs_match_tier(const BbsCart* c, const char* query);
typedef enum { BBS_SORT_BEST, BBS_SORT_STARS, BBS_SORT_NEWEST, BBS_SORT_COUNT } BbsSort;
const char* bbs_sort_name(BbsSort s);
void bbs_sort(BbsCart* carts, int count, const char* query, BbsSort how); // stable
// "Celeste Classic" -> "Celeste Classic.p8.png": nothing a FAT card or the shell dislikes. suffix (may be NULL) goes before the extension.
void bbs_file_name(const char* title, const char* suffix, char* out, size_t cap);
int bbs_is_png(const char* path); // the file starts with the PNG signature and is not tiny

// a curl run in the background (the same idea as the Romulus tool's, with no login)
typedef struct {
	pid_t pid;
	int done;
	int curl_exit;
	int http;
	char out_path[512];
	char code_path[512];
	char err_path[512];
} BbsJob;
int bbs_job_start(BbsJob* job, const char* url, const char* out_path, int download); // 0 or -1
int bbs_job_poll(BbsJob* job); // 1 once finished
void bbs_job_cancel(BbsJob* job);
const char* bbs_job_error(const BbsJob* job); // NULL when it worked
void bbs_job_cleanup(BbsJob* job);
long bbs_job_bytes(const BbsJob* job);
#endif
