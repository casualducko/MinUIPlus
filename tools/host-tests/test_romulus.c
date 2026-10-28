// The Romulus tool's non-UI code (workspace/all/romulus): JSON, zip extraction, config, file
// naming and placement, and the curl job against a mock server (mock_romulus.py).
// Needs ROM_ZIPS (folder from make_zips.py), ROM_PORT and ROM_TOKEN in the environment.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "json.h"
#include "zipx.h"
#include "rapi.h"

static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)

static char work[512];
static char* P(const char* name) { static char b[8][1024]; static int i; i = (i+1)&7; snprintf(b[i], sizeof(b[i]), "%s/%s", work, name); return b[i]; }
static void put(const char* path, const char* text) { FILE* f = fopen(path, "w"); fputs(text, f); fclose(f); }
static long size_of(const char* path) { struct stat st; return stat(path, &st)==0 ? (long)st.st_size : -1; }
static char* slurp(const char* path, size_t* n) {
	FILE* f = fopen(path, "rb"); if (!f) return NULL;
	fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
	char* b = malloc(len+1); *n = fread(b, 1, len, f); b[*n] = 0; fclose(f); return b;
}

static void test_json(void) {
	printf("-- json\n");
	const char* s = "{\"total\": 2, \"roms\": [{\"id\": 5, \"name\": \"A\\u00e9\\n\\\"q\\\"\", \"size\": 1048576.0}, {\"id\": \"7\"}], \"ok\": true, \"n\": null}";
	JVal* v = json_parse(s, strlen(s));
	CHECK(v && v->type==J_OBJ, "object parses");
	CHECK(v && json_int(v, "total", -1)==2, "number");
	JVal* roms = json_get(v, "roms");
	CHECK(roms && roms->type==J_ARR && roms->count==2, "array");
	CHECK(roms && !strcmp(json_str(roms->items[0], "name", ""), "A\xC3\xA9\n\"q\""), "escapes: \\u00e9 as UTF-8, \\n, \\\"");
	CHECK(roms && json_int(roms->items[0], "size", 0)==1048576, "float with .0 reads as an integer");
	CHECK(roms && json_int(roms->items[1], "id", 0)==7, "a number in a string still reads (ids)");
	CHECK(json_int(v, "missing", 42)==42 && !strcmp(json_str(v, "missing", "dflt"), "dflt"), "fallbacks");
	CHECK(json_get(roms, "x")==NULL && json_get(NULL, "x")==NULL, "get on a non-object is NULL");
	json_free(v);

	const char* sur = "\"\\ud83d\\ude00 \\ud83d x\"";
	v = json_parse(sur, strlen(sur));
	CHECK(v && !strncmp(v->str, "\xF0\x9F\x98\x80 ", 5), "surrogate pair becomes one 4 byte character");
	CHECK(v && strstr(v->str, "\xEF\xBF\xBD"), "a lone high surrogate becomes U+FFFD");
	json_free(v);

	const char* bad[] = { "", "{", "{\"a\":1,}", "[1,2", "{\"a\" 1}", "nul", "[1] x", "\"abc", "{\"a\":\"\\q\"}", "\"\x01\"", "[\"\\u12\"]", "{1:2}", NULL };
	int all_null = 1;
	for (int i=0; bad[i]; i++) { JVal* b = json_parse(bad[i], strlen(bad[i])); if (b) { all_null = 0; printf("   parsed: %s\n", bad[i]); json_free(b); } }
	CHECK(all_null, "12 kinds of broken JSON are all refused");
	char deep[200]; memset(deep, '[', 100); memset(deep+100, ']', 100);
	JVal* d = json_parse(deep, 200);
	CHECK(d==NULL, "nesting 100 deep is refused (no stack overflow)");
	JVal* ok = json_parse("[[[[1]]]]", 9);
	CHECK(ok!=NULL, "nesting 4 deep is fine");
	json_free(ok);
	// a document cut off at every length never crashes (run under ASan)
	const char* doc = "{\"total\":3,\"roms\":[{\"id\":1,\"name\":\"x\\u00e9\",\"size\":10},{\"id\":2}]}";
	int crashed = 0;
	for (size_t n=0; n<strlen(doc); n++) { JVal* t = json_parse(doc, n); if (t) crashed = 1; json_free(t); }
	CHECK(!crashed, "every truncation of a document is refused");
}

static void test_config(void) {
	printf("-- config\n");
	RConfig c;
	CHECK(rcfg_load(P("nope.cfg"), &c)==RCFG_MISSING, "no file");
	put(P("a.cfg"), "# c\nurl = https://rom.example.com/// \r\ntoken=\"abc123\"\r\nmap=gba:Game Boy Advance (MGBA)\nmap=bad:../up\nmap=bad2:a/b\nmap=x:.hidden\n");
	CHECK(rcfg_load(P("a.cfg"), &c)==RCFG_OK && !strcmp(c.url, "https://rom.example.com") && !strcmp(c.token, "abc123"), "trailing slashes, CR, quotes");
	CHECK(c.map_count==1 && !strcmp(c.map[0].slug, "gba"), "map lines that are paths are refused");
	put(P("b.cfg"), "url=https://x\n");
	CHECK(rcfg_load(P("b.cfg"), &c)==RCFG_INCOMPLETE, "no token");
	put(P("c.cfg"), "url=http://x\ntoken=t\n");
	CHECK(rcfg_load(P("c.cfg"), &c)==RCFG_NEEDS_HTTPS, "http:// refused");
	put(P("d.cfg"), "url=http://x\ntoken=t\ninsecure_http=1\n");
	CHECK(rcfg_load(P("d.cfg"), &c)==RCFG_OK, "http:// allowed only with insecure_http=1");
	put(P("e.cfg"), "url=ftp://x\ntoken=t\ninsecure_http=1\n");
	CHECK(rcfg_load(P("e.cfg"), &c)==RCFG_NEEDS_HTTPS, "other schemes refused even then");
	put(P("f.cfg"), "url=https://x\ntoken=abc\x01Header: evil\n");
	CHECK(rcfg_load(P("f.cfg"), &c)==RCFG_INCOMPLETE, "a token with control characters is ignored (no header injection)");
	CHECK(rcfg_write_template(P("t.cfg"))==0 && rcfg_load(P("t.cfg"), &c)==RCFG_INCOMPLETE, "template is written and is incomplete");
	put(P("keep.cfg"), "url=https://k\ntoken=k\n");
	rcfg_write_template(P("keep.cfg"));
	CHECK(rcfg_load(P("keep.cfg"), &c)==RCFG_OK, "template never overwrites a config");
	char* big = malloc(5000); memset(big, 'a', 4999); big[4999] = 0;
	char line[6000]; snprintf(line, sizeof(line), "url=https://x\ntoken=%s\n", big);
	put(P("g.cfg"), line);
	CHECK(rcfg_load(P("g.cfg"), &c)==RCFG_OK && strlen(c.token)==sizeof(c.token)-1, "a huge token is cut, not overflowed");
	free(big);
}

static void test_names(void) {
	printf("-- file names and folders\n");
	const SysDef* nes = sys_for_slug("nes");
	const SysDef* gba = sys_for_slug("GBA");
	CHECK(nes && gba && sys_for_slug("famicom")==nes && !sys_for_slug("ps2") && !sys_for_slug(NULL), "slugs map to systems, unknown ones don't");
	char o[300];
	rom_safe_name("Super Game (USA) [!].nes", nes, o, sizeof(o)); CHECK(!strcmp(o, "Super Game (USA) [!].nes"), "ordinary name unchanged");
	rom_safe_name("../../etc/passwd", nes, o, sizeof(o)); CHECK(!strcmp(o, "passwd.nes"), "path stripped, extension added");
	rom_safe_name("..\\..\\x\\Evil Name?.nes", nes, o, sizeof(o)); CHECK(!strcmp(o, "Evil Name_.nes"), "backslash path and ? handled");
	rom_safe_name(".hidden.nes", nes, o, sizeof(o)); CHECK(o[0]!='.', "no hidden files");
	rom_safe_name("a:b*c\"d<e>f|g.gba", gba, o, sizeof(o)); CHECK(!strcmp(o, "a_b_c_d_e_f_g.gba"), "FAT-forbidden characters replaced");
	rom_safe_name("it's $(rm -rf).nes", nes, o, sizeof(o)); CHECK(!strchr(o, '\'') && !strchr(o, '$'), "quote and dollar replaced");
	rom_safe_name("Game.ZIP", nes, o, sizeof(o)); CHECK(!strcmp(o, "Game.ZIP"), "zip kept (it gets unpacked)");
	rom_safe_name("Game.txt", nes, o, sizeof(o)); CHECK(!strcmp(o, "Game.txt.nes"), "unknown extension gets the system's");
	rom_safe_name("noext", gba, o, sizeof(o)); CHECK(!strcmp(o, "noext.gba"), "no extension gets the system's");
	rom_safe_name("", nes, o, sizeof(o)); CHECK(!strcmp(o, "rom.nes"), "empty name");
	rom_safe_name("...", nes, o, sizeof(o)); CHECK(!strcmp(o, "rom.nes"), "only dots");
	rom_safe_name("Game.nes. .", nes, o, sizeof(o)); CHECK(!strcmp(o, "Game.nes"), "trailing dots and spaces trimmed before the extension is judged");
	rom_safe_name("Caf\xC3\xA9 \xE3\x83\x86.nes", nes, o, sizeof(o)); CHECK(!strcmp(o, "Caf\xC3\xA9 \xE3\x83\x86.nes"), "UTF-8 kept");
	char longname[600]; memset(longname, 'e', 500); strcpy(longname+500, ".nes");
	char small[40];
	rom_safe_name(longname, nes, small, sizeof(small)); CHECK(strlen(small)<40, "a long name fits the buffer");
	char acc[600]; for (int i=0;i<250;i++) { acc[i*2] = 0xC3; acc[i*2+1] = 0xA9; } acc[500] = 0;
	char small2[41];
	rom_safe_name(acc, nes, small2, sizeof(small2));
	int whole = 1; for (size_t i=0;i<strlen(small2);i+=2) if ((unsigned char)small2[i]!=0xC3 || (unsigned char)small2[i+1]!=0xA9) whole = 0;
	CHECK(whole && strlen(small2)%2==0, "cut names never end in half a character");

	// folders
	char roms[1024]; snprintf(roms, sizeof(roms), "%s/Roms", work); mkdir(roms, 0777);
	mkdir(P("Roms/GBA (MGBA)"), 0777); mkdir(P("Roms/NES (FC)"), 0777); mkdir(P("Roms/.hidden (GB)"), 0777);
	put(P("Roms/Notes (GB)"), "a file, not a folder");
	extern int has_emu_all(const char*, void*);
	char dir[1024];
	RConfig cfg; memset(&cfg, 0, sizeof(cfg));
	CHECK(rom_dest_dir(&cfg, "gba", gba, roms, has_emu_all, NULL, 1, dir, sizeof(dir))==0 && !strcmp(dir, P("Roms/GBA (MGBA)")), "GBA goes in the existing MGBA folder");
	CHECK(rom_dest_dir(&cfg, "nes", nes, roms, has_emu_all, NULL, 1, dir, sizeof(dir))==0 && !strcmp(dir, P("Roms/NES (FC)")), "NES goes in NES (FC)");
	CHECK(rom_dest_dir(&cfg, "gb", sys_for_slug("gb"), roms, has_emu_all, NULL, 1, dir, sizeof(dir))==0 && !strcmp(dir, P("Roms/Game Boy (GB)")) && access(dir, F_OK)==0, "no folder yet: made as Name (TAG); a hidden folder and a file don't count");
	CHECK(rom_dest_dir(&cfg, "gb", sys_for_slug("gb"), roms, has_emu_all, NULL, 0, dir, sizeof(dir))==0, "again without create: finds it");
	extern int has_emu_none(const char*, void*);
	CHECK(rom_dest_dir(&cfg, "gbc", sys_for_slug("gbc"), roms, has_emu_none, NULL, 1, dir, sizeof(dir))==-1, "no emulator: refused");
	extern int has_emu_gba_only(const char*, void*);
	rmdir(P("Roms/GBA (MGBA)"));
	CHECK(rom_dest_dir(&cfg, "gba", gba, roms, has_emu_gba_only, NULL, 1, dir, sizeof(dir))==0 && !strcmp(dir, P("Roms/Game Boy Advance (GBA)")), "falls to the second tag when the first has no emulator");
	snprintf(cfg.map[0].slug, sizeof(cfg.map[0].slug), "special"); snprintf(cfg.map[0].folder, sizeof(cfg.map[0].folder), "My Folder (X)"); cfg.map_count = 1;
	CHECK(rom_dest_dir(&cfg, "special", NULL, roms, has_emu_none, NULL, 1, dir, sizeof(dir))==0 && !strcmp(dir, P("Roms/My Folder (X)")) && access(dir, F_OK)==0, "map= line decides, even for an unknown slug");

	// request paths
	char path[600];
	rapi_roms_path(path, sizeof(path), NULL, "nes,gb,gba", "Super Mario & co?", 100, 50);
	CHECK(!strcmp(path, "/api/roms?platforms=nes%2Cgb%2Cgba&q=Super+Mario+%26+co%3F&offset=100&limit=50&one=1"), "All tab: platforms=, q encoded like URLEncoder");
	rapi_roms_path(path, sizeof(path), "gba", "nes,gb", "", 0, 50);
	CHECK(!strcmp(path, "/api/roms?platform=gba&q=&offset=0&limit=50&one=1"), "single platform: platform=, empty q, no platforms=");
	rapi_roms_path(path, sizeof(path), NULL, NULL, "caf\xC3\xA9", 0, 50);
	CHECK(strstr(path, "q=caf%C3%A9&")!=NULL, "UTF-8 is percent encoded");
	char tiny[10]; rapi_urlencode("0123456789abcdef", tiny, sizeof(tiny)); CHECK(strlen(tiny)<10, "urlencode respects its buffer");
	rapi_download_path(path, sizeof(path), 12345); CHECK(!strcmp(path, "/api/roms/12345/download"), "download path");

	char ledger[1024]; snprintf(ledger, sizeof(ledger), "%s", P("have.txt"));
	CHECK(!ledger_has(ledger, 5), "ledger: nothing yet");
	ledger_add(ledger, 5); ledger_add(ledger, 5); ledger_add(ledger, 50);
	CHECK(ledger_has(ledger, 5) && ledger_has(ledger, 50) && !ledger_has(ledger, 0) && !ledger_has(ledger, 500), "ledger: exact ids");
	CHECK(size_of(ledger)==5, "ledger: no duplicates (5\\n50\\n)");
}
int has_emu_all(const char* tag, void* c) { (void)tag; (void)c; return 1; }
int has_emu_none(const char* tag, void* c) { (void)tag; (void)c; return 0; }
int has_emu_gba_only(const char* tag, void* c) { (void)c; return !strcmp(tag, "GBA"); }

static void test_zip(const char* zips) {
	printf("-- zip\n");
	const char* exts[] = { "nes", "gba", NULL };
	ZipEntry e;
	char zp[1024], out[1024];
	snprintf(zp, sizeof(zp), "%s/ok.zip", zips);
	CHECK(zipx_find(zp, exts, &e)==ZIPX_OK && !strcmp(e.name, "Game One.nes"), "finds the first ROM entry, skipping readme.txt, base name only");
	snprintf(out, sizeof(out), "%s", P("extracted.nes"));
	CHECK(zipx_extract(zp, &e, out)==ZIPX_OK && size_of(out)==100004, "deflated entry extracts to the right size (CRC checked)");
	snprintf(zp, sizeof(zp), "%s/stored.zip", zips);
	CHECK(zipx_find(zp, exts, &e)==ZIPX_OK && e.method==0 && zipx_extract(zp, &e, P("stored.gba"))==ZIPX_OK && size_of(P("stored.gba"))==100004, "stored entry");
	snprintf(zp, sizeof(zp), "%s/evil.zip", zips);
	CHECK(zipx_find(zp, exts, &e)==ZIPX_OK && !strcmp(e.name, "evil.nes"), "../../ in the entry name: only the base name is used");
	snprintf(zp, sizeof(zp), "%s/evil2.zip", zips);
	CHECK(zipx_find(zp, exts, &e)==ZIPX_OK && !strcmp(e.name, "evil2.nes"), "backslash path in the entry name too");
	snprintf(zp, sizeof(zp), "%s/folder.zip", zips);
	CHECK(zipx_find(zp, exts, &e)==ZIPX_OK && !strcmp(e.name, "Real.nes"), "folder entries skipped");
	snprintf(zp, sizeof(zp), "%s/upper.zip", zips);
	CHECK(zipx_find(zp, exts, &e)==ZIPX_OK && !strcmp(e.name, "GAME.NES"), "extension match ignores case");
	snprintf(zp, sizeof(zp), "%s/norom.zip", zips);
	CHECK(zipx_find(zp, exts, &e)==ZIPX_E_NONE, "no ROM inside");

	remove(P("c.nes"));
	snprintf(zp, sizeof(zp), "%s/corrupt.zip", zips);
	int f = zipx_find(zp, exts, &e);
	CHECK(f==ZIPX_OK && zipx_extract(zp, &e, P("c.nes"))!=ZIPX_OK && access(P("c.nes"), F_OK)!=0, "corrupt data: refused, and no partial file left");
	snprintf(zp, sizeof(zp), "%s/badcrc.zip", zips);
	f = zipx_find(zp, exts, &e);
	CHECK(f==ZIPX_OK && zipx_extract(zp, &e, P("c2.gba"))==ZIPX_E_CORRUPT && access(P("c2.gba"), F_OK)!=0, "wrong CRC: refused, no file");
	snprintf(zp, sizeof(zp), "%s/truncated.zip", zips);
	CHECK(zipx_find(zp, exts, &e)<0, "truncated zip: refused");
	snprintf(zp, sizeof(zp), "%s/notzip.zip", zips);
	CHECK(zipx_find(zp, exts, &e)==ZIPX_E_NOTZIP, "not a zip");
	snprintf(zp, sizeof(zp), "%s/empty.zip", zips);
	CHECK(zipx_find(zp, exts, &e)==ZIPX_E_NOTZIP, "empty file");
	CHECK(zipx_find(P("does-not-exist.zip"), exts, &e)==ZIPX_E_OPEN, "missing file");
	snprintf(zp, sizeof(zp), "%s/huge.zip", zips);
	CHECK(zipx_find(zp, exts, &e)==ZIPX_E_UNSUPPORTED, "an entry claiming 2 GB is refused");
	snprintf(zp, sizeof(zp), "%s/bomb.zip", zips);
	f = zipx_find(zp, exts, &e);
	CHECK(f==ZIPX_OK && zipx_extract(zp, &e, P("bomb.nes"))!=ZIPX_OK && access(P("bomb.nes"), F_OK)!=0, "a stream that expands past its declared size stops (no 5 MB written)");
	snprintf(zp, sizeof(zp), "%s/encrypted.zip", zips);
	CHECK(zipx_find(zp, exts, &e)==ZIPX_E_UNSUPPORTED, "encrypted entry refused");
	// fuzz: damage the zip at every position of the first 600 bytes and of the directory
	snprintf(zp, sizeof(zp), "%s/ok.zip", zips);
	size_t n; char* data = slurp(zp, &n);
	int hang_or_crash = 0;
	for (size_t i=0; i<n; i += (i<600 || i+400>n) ? 1 : 97) {
		char saved = data[i]; data[i] ^= 0xA5;
		FILE* fz = fopen(P("fuzz.zip"), "wb"); fwrite(data, 1, n, fz); fclose(fz);
		ZipEntry fe;
		if (zipx_find(P("fuzz.zip"), exts, &fe)==ZIPX_OK) zipx_extract(P("fuzz.zip"), &fe, P("fuzz.out"));
		data[i] = saved;
	}
	CHECK(!hang_or_crash, "single-byte damage anywhere in the zip never crashes (ASan)");
	free(data);
}

static const RConfig* cfg_ptr;
static int run_job(RJob* job, const char* path, const char* out, int download) {
	if (rjob_start(job, cfg_ptr, path, out, download)!=0) return -1;
	for (int i=0; i<2000 && !rjob_poll(job); i++) usleep(5000);
	return job->done ? 0 : -2;
}
static void test_server(void) {
	printf("-- curl job against the mock server\n");
	const char* port = getenv("ROM_PORT"); const char* token = getenv("ROM_TOKEN");
	RConfig cfg; memset(&cfg, 0, sizeof(cfg));
	snprintf(cfg.url, sizeof(cfg.url), "http://127.0.0.1:%s", port); snprintf(cfg.token, sizeof(cfg.token), "%s", token);
	cfg.insecure = 1; cfg_ptr = &cfg;
	RJob job; char path[600], out[1024]; snprintf(out, sizeof(out), "%s", P("resp.json"));
	size_t n; char* body;

	rapi_platforms_path(path, sizeof(path));
	CHECK(run_job(&job, path, out, 0)==0 && !rjob_error(&job), "platforms: HTTP 200");
	body = slurp(out, &n);
	RPlat plats[16]; int pc = rapi_parse_platforms(body, n, plats, 16); free(body);
	int nes = 0, gba = 0; for (int i=0;i<pc;i++) { if (!strcmp(plats[i].slug, "nes")) nes = plats[i].count; if (!strcmp(plats[i].slug, "gba")) gba = plats[i].count; }
	CHECK(pc==4 && nes==73 && gba==31, "platforms parsed with counts");
	rjob_cleanup(&job);

	RRomList list = {0}; long total = 0; int raw = 0;
	rapi_roms_path(path, sizeof(path), "nes", NULL, "", 0, 50);
	run_job(&job, path, out, 0); body = slurp(out, &n);
	int added = rapi_parse_roms(body, n, &list, &total, &raw); free(body); rjob_cleanup(&job);
	CHECK(added==50 && raw==50 && total==73 && list.count==50, "first page of nes: 50 of 73");
	rapi_roms_path(path, sizeof(path), "nes", NULL, "", 50, 50);
	run_job(&job, path, out, 0); body = slurp(out, &n);
	added = rapi_parse_roms(body, n, &list, &total, &raw); free(body); rjob_cleanup(&job);
	CHECK(added==23 && list.count==73, "second page appends the other 23");
	int dup = 0; for (int i=0;i<list.count;i++) for (int j=i+1;j<list.count;j++) if (list.items[i].id==list.items[j].id) dup = 1;
	CHECK(!dup, "no game twice across pages");
	rromlist_sort_newest(&list);
	int desc = 1; for (int i=1;i<list.count;i++) if (list.items[i-1].id<list.items[i].id) desc = 0;
	CHECK(desc && list.count==73, "newest first: highest id on top, nothing lost");
	rromlist_free(&list);

	// disc sets are left out after the page is cut: 20 gb games, 4 of them dirs, pages of 10
	rapi_roms_path(path, sizeof(path), "gb", NULL, "", 0, 10);
	run_job(&job, path, out, 0); body = slurp(out, &n);
	added = rapi_parse_roms(body, n, &list, &total, &raw); free(body); rjob_cleanup(&job);
	int first = added;
	rapi_roms_path(path, sizeof(path), "gb", NULL, "", 10, 10);
	run_job(&job, path, out, 0); body = slurp(out, &n);
	added = rapi_parse_roms(body, n, &list, &total, &raw); free(body); rjob_cleanup(&job);
	int dup2 = 0; for (int i=0;i<list.count;i++) for (int j=i+1;j<list.count;j++) if (list.items[i].id==list.items[j].id) dup2 = 1;
	CHECK(first==8 && added==8 && total==20 && !dup2 && list.count==16, "short pages (disc sets dropped): paging by the page size gives every game once");
	rromlist_free(&list);
	
	rapi_roms_path(path, sizeof(path), NULL, "nes,gba", "game 01", 0, 50);
	run_job(&job, path, out, 0); body = slurp(out, &n);
	added = rapi_parse_roms(body, n, &list, &total, &raw); free(body); rjob_cleanup(&job);
	int both = 0, g = 0; for (int i=0;i<list.count;i++) { if (!strcmp(list.items[i].platform, "nes")) both |= 1; if (!strcmp(list.items[i].platform, "gba")) both |= 2; if (!strncmp(list.items[i].name, "NES Game 01", 11)) g++; }
	CHECK(total==20 && both==3 && g==10, "All tab with a search: both platforms, only matches");
	rromlist_free(&list);

	rapi_roms_path(path, sizeof(path), NULL, NULL, "caf\xC3\xA9", 0, 50);
	run_job(&job, path, out, 0); body = slurp(out, &n);
	added = rapi_parse_roms(body, n, &list, &total, &raw); free(body); rjob_cleanup(&job);
	CHECK(added==1 && !strcmp(list.items[0].name, "Caf\xC3\xA9 \xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88 \xF0\x9F\x98\x80") && list.items[0].id==902, "a UTF-8 search finds a UTF-8 name, emoji included");
	rromlist_free(&list);

	rapi_download_path(path, sizeof(path), 5);
	CHECK(run_job(&job, path, P("dl.bin"), 1)==0 && !rjob_error(&job) && size_of(P("dl.bin"))==(long)strlen("ROM5-")*700, "download: exact bytes");
	rjob_cleanup(&job);

	// the token must not be on the command line
	RJob slow; rapi_download_path(path, sizeof(path), 901);
	CHECK(rjob_start(&slow, &cfg, path, P("slow.bin"), 1)==0, "slow download starts");
	char cmd[300]; snprintf(cmd, sizeof(cmd), "ps -o args= -p %d", (int)slow.pid);
	FILE* ps = popen(cmd, "r"); char psline[1024] = ""; if (ps) { fread(psline, 1, sizeof(psline)-1, ps); pclose(ps); }
	CHECK(psline[0] && !strstr(psline, token), "the token is not in the process arguments");
	long seen = 0; for (int i=0; i<200 && seen<=0; i++) { usleep(20000); seen = rjob_bytes(&slow); }
	CHECK(seen>0 && !rjob_poll(&slow), "progress is visible while it runs");
	rjob_cancel(&slow);
	CHECK(slow.done && !strcmp(rjob_error(&slow), "Cancelled") && rjob_poll(&slow), "cancel stops it");
	rjob_cleanup(&slow); unlink(P("slow.bin"));

	// errors, as the Android app words them
	RConfig bad = cfg; snprintf(bad.token, sizeof(bad.token), "wrong"); cfg_ptr = &bad;
	rapi_platforms_path(path, sizeof(path)); run_job(&job, path, out, 0);
	CHECK(job.http==401 && !strcmp(rjob_error(&job), "Wrong token"), "401: Wrong token"); rjob_cleanup(&job);
	cfg_ptr = &cfg;
	run_job(&job, "/nothing/here", out, 0);
	CHECK(job.http==404 && strstr(rjob_error(&job), "no API"), "404: Server has no API ..."); rjob_cleanup(&job);
	rapi_download_path(path, sizeof(path), 99999); run_job(&job, path, out, 1);
	CHECK(job.http==404, "unknown game id: 404"); rjob_cleanup(&job);
	RConfig down = cfg; snprintf(down.url, sizeof(down.url), "http://127.0.0.1:1"); cfg_ptr = &down;
	rapi_platforms_path(path, sizeof(path)); run_job(&job, path, out, 0);
	CHECK(job.curl_exit==7 && !strcmp(rjob_error(&job), "Can't connect to the server"), "nothing listening: Can't connect"); rjob_cleanup(&job);
	snprintf(down.url, sizeof(down.url), "http://no-such-host.invalid"); run_job(&job, path, out, 0);
	CHECK(job.curl_exit==6 && strstr(rjob_error(&job), "Can't find"), "unknown host: Can't find the server"); rjob_cleanup(&job);
	RConfig tls = cfg; snprintf(tls.url, sizeof(tls.url), "https://127.0.0.1:%s", port); tls.insecure = 0; cfg_ptr = &tls;
	run_job(&job, path, out, 0);
	CHECK(job.curl_exit!=0 && rjob_error(&job), "https to a plain http port fails cleanly"); rjob_cleanup(&job);
	RConfig plain = cfg; plain.insecure = 0; cfg_ptr = &plain; // http:// with insecure off: curl itself refuses
	run_job(&job, path, out, 0);
	CHECK(job.curl_exit!=0 && rjob_error(&job), "http:// is refused by curl when insecure is off (--proto)"); rjob_cleanup(&job);
	cfg_ptr = &cfg;

	// a job that is never polled to the end and cancelled twice is fine
	RJob j2; rapi_platforms_path(path, sizeof(path));
	rjob_start(&j2, &cfg, path, out, 0); rjob_cancel(&j2); rjob_cancel(&j2); rjob_cleanup(&j2);
	CHECK(j2.done, "cancel twice is harmless");
}

int main(int argc, char** argv) {
	(void)argc; (void)argv;
	const char* tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
	snprintf(work, sizeof(work), "%s/romulus-test-%d", tmp, (int)getpid());
	mkdir(work, 0777);
	const char* zips = getenv("ROM_ZIPS");
	test_json();
	test_config();
	test_names();
	if (zips) test_zip(zips); else printf("SKIPPED zip tests (no ROM_ZIPS)\n");
	if (getenv("ROM_PORT")) test_server(); else printf("SKIPPED server tests (no ROM_PORT)\n");
	char cmd[600]; snprintf(cmd, sizeof(cmd), "rm -rf '%s'", work); system(cmd);
	printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
	return fails != 0;
}
