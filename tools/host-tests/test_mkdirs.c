// mkdirs() from utils.c: mkdir -p without a shell (minarch used to run `mkdir -p "<tag>..."` through one)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#define MAX_PATH 1024
#include "gen/mkdirs.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static int isdir(const char* p) { struct stat st; return stat(p, &st)==0 && S_ISDIR(st.st_mode); }
int main(void) {
	char root[512], p[1100];
	snprintf(root, sizeof(root), "%s/mkdirs-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
	CHECK(mkdirs(root)==0 && isdir(root), "a folder under an existing one");
	snprintf(p, sizeof(p), "%s/a/b/c/d", root);
	CHECK(mkdirs(p)==0 && isdir(p), "nested folders in one call");
	CHECK(mkdirs(p)==0, "already there: fine");
	snprintf(p, sizeof(p), "%s/a/file", root); FILE* f = fopen(p, "w"); fputs("x", f); fclose(f);
	CHECK(mkdirs(p)==-1, "a file in the way: fails");
	snprintf(p, sizeof(p), "%s/a/file/under", root);
	CHECK(mkdirs(p)==-1, "under a file: fails");
	// a hostile tag: backticks and $() are just characters, nothing runs
	snprintf(p, sizeof(p), "%s/x/`touch %s/pwned`-gambatte", root, root);
	CHECK(mkdirs(p)==0 && isdir(p), "backticks in a name make a folder with that name");
	char pw[600]; snprintf(pw, sizeof(pw), "%s/pwned", root);
	CHECK(access(pw, F_OK)!=0, "and the command inside was not run");
	snprintf(p, sizeof(p), "%s/y/$(touch %s/pwned2)-x\";touch %s/pwned3;\"", root, root, root);
	mkdirs(p);
	snprintf(pw, sizeof(pw), "%s/pwned2", root); int a = access(pw, F_OK)!=0;
	snprintf(pw, sizeof(pw), "%s/pwned3", root); int b = access(pw, F_OK)!=0;
	CHECK(a && b, "$() and a quote that tries to end the string: nothing runs");
	CHECK(mkdirs("")==-1 && mkdirs(NULL)==-1, "empty and NULL");
	char big[3000]; memset(big, 'a', sizeof(big)-1); big[0] = '/'; big[sizeof(big)-1] = 0;
	CHECK(mkdirs(big)==-1, "a path longer than MAX_PATH is refused, not overflowed");
	char cmd[700]; snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root); system(cmd);
	printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
	return fails != 0;
}
