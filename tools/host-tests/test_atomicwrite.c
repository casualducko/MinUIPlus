// writeFileAtomic() from utils.c: a failed save must keep the old file (SRAM, RTC and state saves use it)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#define MAX_PATH 1024
#include "gen/writeFileAtomic.inc"
#include "gen/putFile.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
static char* slurp(const char* p, size_t* n) { FILE* f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); char* b = malloc(l+1); *n = fread(b, 1, l, f); b[*n] = 0; fclose(f); return b; }
int main(void) {
	char dir[512], p[700], tmp[720];
	snprintf(dir, sizeof(dir), "%s/atomic-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
	mkdir(dir, 0777);
	snprintf(p, sizeof(p), "%s/Game.sav", dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", p);
	size_t n; char* c;
	CHECK(writeFileAtomic(p, "first save", 10)==0, "writes a new file");
	c = slurp(p, &n); CHECK(c && n==10 && !memcmp(c, "first save", 10), "with the right contents"); free(c);
	CHECK(access(tmp, F_OK)!=0, "no temp file left behind");
	CHECK(writeFileAtomic(p, "second", 6)==0, "replaces an existing file");
	c = slurp(p, &n); CHECK(c && n==6 && !memcmp(c, "second", 6), "with only the new contents (not a mix)"); free(c);
	// a failing write keeps the old file: the folder disappears under the temp file
	char bad[800]; snprintf(bad, sizeof(bad), "%s/no/such/dir/Game.sav", dir);
	CHECK(writeFileAtomic(bad, "x", 1)==-1, "can't create the temp file: fails");
	// an unwritable temp location: a directory sits where the temp file would go
	mkdir(tmp, 0777);
	CHECK(writeFileAtomic(p, "third", 5)==-1, "temp path blocked: fails");
	c = slurp(p, &n); CHECK(c && n==6 && !memcmp(c, "second", 6), "and the previous save is untouched"); free(c);
	rmdir(tmp);
	// the data pointer being NULL with a size is a failed write, not a crash; the old file stays
	CHECK(writeFileAtomic(p, NULL, 5)==-1, "NULL data with a size: fails");
	c = slurp(p, &n); CHECK(c && n==6, "old file still there"); free(c);
	CHECK(writeFileAtomic(p, NULL, 0)==0, "size 0 writes an empty file");
	c = slurp(p, &n); CHECK(c && n==0, "(a core with no memory)"); free(c);
	// putFile on a device/sysfs-like file must write in place (bl_power, cpu governor): no temp file, node stays
	putFile("/dev/null", "1");
	struct stat dst; CHECK(stat("/dev/null", &dst)==0 && S_ISCHR(dst.st_mode), "putFile on a device node leaves it a device node");
	putFile(p, "plain");
	c = slurp(p, &n); CHECK(c && n==5 && !memcmp(c, "plain", 5), "putFile on a regular file still replaces it"); free(c);
	// big (an 8 MB state)
	size_t big = 8*1024*1024; char* data = malloc(big); for (size_t i=0;i<big;i++) data[i] = (char)(i*31);
	CHECK(writeFileAtomic(p, data, big)==0, "8 MB state");
	c = slurp(p, &n); CHECK(c && n==big && !memcmp(c, data, big), "identical"); free(c); free(data);
	char longpath[2000]; memset(longpath, 'a', sizeof(longpath)-1); longpath[sizeof(longpath)-1] = 0;
	CHECK(writeFileAtomic(longpath, "x", 1)==-1 && writeFileAtomic(NULL, "x", 1)==-1, "an over-long path or NULL is refused");
	char cmd[700]; snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir); system(cmd);
	printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
	return fails != 0;
}
