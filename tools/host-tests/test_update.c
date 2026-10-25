// the Update tool's parsing (update_core.c)
#include <stdio.h>
#include <string.h>
#include "update_core.c"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
int main(void) {
	char repo[128], token[128];
	Update_parseConfig(NULL, repo, sizeof(repo), token, sizeof(token));
	CHECK(!strcmp(repo, UPDATE_DEFAULT_REPO) && !token[0], "no config: the default repo, no token");
	Update_parseConfig("# a comment\nrepo = someone/Thing \r\ntoken=abc123\n\n  \nbogus line\n", repo, sizeof(repo), token, sizeof(token));
	CHECK(!strcmp(repo, "someone/Thing") && !strcmp(token, "abc123"), "repo and token, spaces and CRLF trimmed");
	Update_parseConfig("repo=\ntoken=\n", repo, sizeof(repo), token, sizeof(token));
	CHECK(!strcmp(repo, UPDATE_DEFAULT_REPO) && !token[0], "an empty repo keeps the default");
	char tiny[6];
	Update_parseConfig("repo=owner/long-name\n", tiny, sizeof(tiny), token, sizeof(token));
	CHECK(strlen(tiny)==5, "a small buffer is cut, not overrun");

	const char* json =
	"{\"url\":\"https://api.github.com/repos/o/r/releases/1\",\"assets_url\":\"https://api.github.com/repos/o/r/releases/1/assets\",\"id\":1,"
	"\"author\":{\"login\":\"x\",\"url\":\"https://api.github.com/users/x\"},\"tag_name\":\"v2026.10.25.1\",\"name\":\"Build\","
	"\"assets\":[{\"url\":\"https://api.github.com/repos/o/r/releases/assets/11\",\"id\":11,\"name\":\"MinUIPlus-v2026.10.25.1.tar.gz.sha256\","
	"\"uploader\":{\"url\":\"https://api.github.com/users/x\"},\"size\":97,\"state\":\"uploaded\"},"
	"{\"url\":\"https://api.github.com/repos/o/r/releases/assets/12\",\"id\":12,\"name\":\"MinUIPlus-v2026.10.25.1.tar.gz\","
	"\"uploader\":{\"url\":\"https://api.github.com/users/x\"},\"size\": 22834567,\"state\":\"uploaded\"}],\"body\":\"notes\"}";
	UpdateRelease r;
	CHECK(Update_parseRelease(json, &r), "a release with a package and its sum");
	CHECK(!strcmp(r.tag, "v2026.10.25.1"), "the tag");
	CHECK(!strcmp(r.pkg_name, "MinUIPlus-v2026.10.25.1.tar.gz"), "the package name");
	CHECK(!strcmp(r.pkg_url, "https://api.github.com/repos/o/r/releases/assets/12"), "the package url (not the sum's)");
	CHECK(!strcmp(r.sum_url, "https://api.github.com/repos/o/r/releases/assets/11"), "the sum url");
	CHECK(r.pkg_size==22834567ULL, "the package size, with a space after the colon");
	CHECK(!Update_parseRelease("{\"message\":\"Not Found\"}", &r), "a not-found answer");
	CHECK(!Update_parseRelease("", &r) && !Update_parseRelease(NULL, &r), "empty and NULL");
	CHECK(!Update_parseRelease("{\"tag_name\":\"v1\",\"assets\":[]}", &r), "a release with no assets");
	CHECK(!Update_parseRelease("{\"tag_name\":\"v1\",\"assets\":[{\"url\":\"https://x/releases/assets/5\",\"name\":\"a.tar.gz\",\"size\":5}]}", &r), "a package without its sum is refused");
	CHECK(!Update_parseRelease("{\"tag_name\":\"v1\",\"assets\":[{\"url\":\"https://x/releases/assets/5\",\"name\":\"a.zip\",\"size\":5},{\"url\":\"https://x/releases/assets/6\",\"name\":\"a.zip.sha256\",\"size\":5}]}", &r), "no .tar.gz asset");
	CHECK(!Update_parseRelease("{\"tag_name\":\"\",\"assets\":[]}", &r), "an empty tag");

	CHECK(Update_compareVersions("v2026.10.25.2", "v2026.10.25.1")>0, "newer build the same day");
	CHECK(Update_compareVersions("v2026.10.25.1", "v2026.10.25.1")==0, "same");
	CHECK(Update_compareVersions("v2026.9.30", "v2026.10.1")<0, "9 < 10 as numbers, not text");
	CHECK(Update_compareVersions("v2026.10.25", "v2026.10.25.0")==0, "a missing part is zero");
	CHECK(Update_compareVersions("", "v1")<0 && Update_compareVersions(NULL, NULL)==0, "empty and NULL");
	CHECK(Update_compareVersions("v2027.1.1", "v2026.12.31")>0, "next year");

	char sum[65];
	CHECK(Update_parseSum("ABCDEF0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789  file.tar.gz\n", sum) && !strncmp(sum, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789", 64), "a sha256sum line, lower-cased");
	CHECK(Update_parseSum("\n  0123456789012345678901234567890123456789012345678901234567890123", sum), "a bare hash with leading blanks");
	CHECK(!Update_parseSum("0123", sum), "too short");
	CHECK(!Update_parseSum("01234567890123456789012345678901234567890123456789012345678901234", sum), "too long");
	CHECK(!Update_parseSum("<html>Not Found</html>", sum), "an error page");
	CHECK(!Update_parseSum(NULL, sum), "NULL");

	CHECK(Update_pathsSafe("install.sh\n.system/rgb30/bin/minui.elf\nTools/rgb30/Info.pak/\n"), "an ordinary listing");
	CHECK(!Update_pathsSafe("install.sh\n/etc/passwd\n"), "an absolute path");
	CHECK(!Update_pathsSafe("a/../../etc/x\n"), "a .. in the middle");
	CHECK(!Update_pathsSafe("../x\n"), "a leading ..");
	CHECK(!Update_pathsSafe("a/..\n"), "a trailing ..");
	CHECK(Update_pathsSafe("a/..b/c...d/.hidden\n"), "dots in names are fine");
	CHECK(!Update_pathsSafe("") && !Update_pathsSafe(NULL), "an empty listing is not a package");

	CHECK(Update_repoValid("casualducko/MinUIPlus") && Update_repoValid("a-b/c_d.e"), "good repos");
	CHECK(!Update_repoValid("") && !Update_repoValid("noslash") && !Update_repoValid("a/b/c") && !Update_repoValid("/b") && !Update_repoValid("a/"), "bad shapes");
	CHECK(!Update_repoValid("a/b c") && !Update_repoValid("a/b;rm") && !Update_repoValid("a/../b") && !Update_repoValid("a/b?x=1"), "unsafe characters");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
