// The Wi-Fi tool's logic: what iw prints, the saved networks file, running commands
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "wifi_core.h"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)

static const char* SCAN =
"BSS 00:11:22:33:44:55(on wlan0)\n"
"\tlast seen: 1234.567s [boottime]\n"
"\tTSF: 1 usec (0d, 00:00:00)\n"
"\tfreq: 2437\n"
"\tbeacon interval: 100 TUs\n"
"\tcapability: ESS Privacy ShortSlotTime (0x0411)\n"
"\tsignal: -52.00 dBm\n"
"\tlast seen: 1710 ms ago\n"
"\tSSID: HomeNet\n"
"\tSupported rates: 1.0* 2.0* 5.5* 11.0* 6.0 9.0 12.0 18.0\n"
"\tRSN:\t * Version: 1\n"
"\t\t * Group cipher: CCMP\n"
"\t\t * Pairwise ciphers: CCMP\n"
"\t\t * Authentication suites: PSK\n"
"BSS 00:11:22:33:44:56(on wlan0)\n"
"\tfreq: 5180\n"
"\tcapability: ESS Privacy ShortSlotTime (0x0411)\n"
"\tsignal: -67.00 dBm\n"
"\tSSID: HomeNet\n"
"\tRSN:\t * Version: 1\n"
"BSS aa:bb:cc:dd:ee:ff(on wlan0) -- associated\n"
"\tfreq: 2412\n"
"\tcapability: ESS ShortSlotTime (0x0401)\n"
"\tsignal: -81.00 dBm\n"
"\tSSID: Coffee Shop Guest\n"
"BSS 10:20:30:40:50:60(on wlan0)\n"
"\tcapability: ESS Privacy (0x0011)\n"
"\tsignal: -70.00 dBm\n"
"\tSSID: \\x00\\x00\\x00\\x00\n"
"BSS 10:20:30:40:50:61(on wlan0)\n"
"\tsignal: -60.00 dBm\n"
"\tSSID: \n"
"BSS 11:22:33:44:55:66(on wlan0)\n"
"\tcapability: ESS Privacy (0x0011)\n"
"\tsignal: -40.50 dBm\n"
"\tSSID: Caf\\xc3\\xa9 \\xe3\\x81\\x82\n"
"\tWPA:\t * Version: 1\n"
"BSS 77:88:99:aa:bb:cc(on wlan0)\n"
"\tcapability: ESS Privacy (0x0011)\n"
"\tsignal: -90.00 dBm\n"
"\tSSID: OldWEP\n";

static void put(const char* path, const char* body) { FILE* f = fopen(path, "w"); fputs(body, f); fclose(f); chmod(path, 0755); }

int main(void) {
	char out[256];
	CHECK(wifi_ssid_unescape("plain", out, sizeof(out))==5 && !strcmp(out, "plain"), "plain names are unchanged");
	CHECK(wifi_ssid_unescape("Caf\\xc3\\xa9", out, sizeof(out))==5 && !strcmp(out, "Caf\xc3\xa9"), "\\xNN escapes become bytes (UTF-8)");
	CHECK(wifi_ssid_unescape("a\\xZZb", out, sizeof(out))==6 && !strcmp(out, "a\\xZZb"), "a bad escape is left as typed");
	CHECK(wifi_ssid_unescape("abcdef", out, 4)==3 && !strcmp(out, "abc"), "never writes past the buffer");

	WifiNet nets[WIFI_MAX_NETS];
	int n = wifi_parse_scan(SCAN, nets, WIFI_MAX_NETS);
	CHECK(n==4, "4 named networks (the hidden ones are left out)");
	CHECK(!strcmp(nets[0].ssid, "Caf\xc3\xa9 \xe3\x81\x82") && nets[0].signal==-41, "strongest first (and UTF-8 names decode; -40.5 rounds away from zero)");
	CHECK(nets[0].secured, "a WPA: line means secured");
	CHECK(!strcmp(nets[1].ssid, "HomeNet") && nets[1].signal==-52 && nets[1].secured, "two access points with one name: one entry, the stronger signal");
	CHECK(!strcmp(nets[2].ssid, "Coffee Shop Guest") && !nets[2].secured, "no Privacy and no RSN: open (names with spaces are fine)");
	CHECK(!strcmp(nets[3].ssid, "OldWEP") && nets[3].secured && nets[3].signal==-90, "Privacy alone (WEP) counts as secured");
	CHECK(wifi_parse_scan("", nets, WIFI_MAX_NETS)==0 && wifi_parse_scan("garbage\nmore\n", nets, WIFI_MAX_NETS)==0, "nothing or nonsense: no networks");
	CHECK(wifi_parse_scan(SCAN, nets, 2)==2, "never more than the room it was given");

	const char* SERVICES =
"*AO HomeNet              wifi_dc85de828967_686f6d654e6574_managed_psk\n"
"    Coffee Shop Guest    wifi_dc85de828967_636f66_managed_none\n"
"*AR Work 5G              wifi_dc85de828967_776f726b_managed_psk\n"
"                         wifi_dc85de828967_hidden_managed_psk\n"
"    Ethernet-ish         ethernet_aabbcc_cable\n"
"    HomeNet              wifi_dc85de828967_686f6d654e6574_other_psk\n";
	n = wifi_parse_services(SERVICES, nets, WIFI_MAX_NETS);
	CHECK(n==3, "connman services: 3 named wifi networks (hidden, non-wifi and a repeated name are skipped)");
	CHECK(!strcmp(nets[0].ssid, "HomeNet") && nets[0].secured && !strcmp(nets[1].ssid, "Coffee Shop Guest") && !nets[1].secured && !strcmp(nets[2].ssid, "Work 5G"), "names with spaces, and _none means open");
	CHECK(nets[0].signal==-70, "no strength there: a middling one is filled in");
	CHECK(wifi_parse_services("", nets, WIFI_MAX_NETS)==0 && wifi_parse_services("Usage: connmanctl\n", nets, WIFI_MAX_NETS)==0, "nothing or help text: no networks");

	char ssid[WIFI_SSID_MAX]; int sig = 0;
	const char* LINK = "Connected to 00:11:22:33:44:55 (on wlan0)\n\tSSID: HomeNet\n\tfreq: 2437\n\tRX: 1 bytes (1 packets)\n\tsignal: -48 dBm\n\trx bitrate: 72.2 MBit/s\n";
	CHECK(wifi_parse_link(LINK, ssid, sizeof(ssid), &sig) && !strcmp(ssid, "HomeNet") && sig==-48, "link: connected, name and signal");
	CHECK(!wifi_parse_link("Not connected.\n", ssid, sizeof(ssid), &sig) && !ssid[0], "link: not connected");
	CHECK(!wifi_parse_link("", ssid, sizeof(ssid), &sig) && !wifi_parse_link(NULL, ssid, sizeof(ssid), &sig), "link: empty or NULL is not connected");

	CHECK(wifi_bars(-40)==4 && wifi_bars(-55)==4 && wifi_bars(-56)==3 && wifi_bars(-66)==2 && wifi_bars(-80)==1 && wifi_bars(-95)==0, "bars from dBm");
	CHECK(!strcmp(wifi_bars_label(4), "Excellent") && !strcmp(wifi_bars_label(0), "Very weak") && !strcmp(wifi_bars_label(9), "Excellent"), "labels");

	// saved networks
	char dir[512], path[600];
	snprintf(dir, sizeof(dir), "%s/wifi-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
	mkdir(dir, 0777);
	snprintf(path, sizeof(path), "%s/wifi-networks.txt", dir);
	WifiSaved list[WIFI_MAX_NETS];
	int count = wifi_saved_load(path, list, WIFI_MAX_NETS);
	CHECK(count==0, "no file: nothing saved");
	count = wifi_saved_put(list, count, WIFI_MAX_NETS, "Home", "pass one");
	count = wifi_saved_put(list, count, WIFI_MAX_NETS, "Work\tNet", "tab\\and\nnewline");
	count = wifi_saved_put(list, count, WIFI_MAX_NETS, "Cafe", "");
	CHECK(count==3 && !strcmp(list[0].ssid, "Cafe") && !strcmp(list[2].ssid, "Home"), "the newest is first");
	count = wifi_saved_put(list, count, WIFI_MAX_NETS, "Home", "pass two");
	CHECK(count==3 && !strcmp(list[0].ssid, "Home") && !strcmp(list[0].pass, "pass two"), "saving a known name updates it (no duplicate) and moves it first");
	CHECK(wifi_saved_store(path, list, count)==0, "stores");
	WifiSaved back[WIFI_MAX_NETS];
	int c2 = wifi_saved_load(path, back, WIFI_MAX_NETS);
	CHECK(c2==3 && !strcmp(back[0].ssid, "Home") && !strcmp(back[0].pass, "pass two"), "reads back the same order and passwords");
	int w = wifi_saved_find(back, c2, "Work\tNet");
	CHECK(w>=0 && !strcmp(back[w].pass, "tab\\and\nnewline"), "tabs, backslashes and newlines in names and passwords survive");
	int cafe = wifi_saved_find(back, c2, "Cafe");
	CHECK(cafe>=0 && back[cafe].pass[0]=='\0', "an open network has an empty password");
	CHECK(wifi_saved_find(back, c2, "Nope")==-1, "find: missing");
	c2 = wifi_saved_remove(back, c2, wifi_saved_find(back, c2, "Home"));
	CHECK(c2==2 && wifi_saved_find(back, c2, "Home")==-1 && wifi_saved_find(back, c2, "Cafe")>=0, "remove");
	CHECK(wifi_saved_remove(back, c2, 7)==2 && wifi_saved_remove(back, c2, -1)==2, "remove out of range changes nothing");
	char tmp[700]; snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	CHECK(access(tmp, F_OK)!=0, "no temp file left behind");
	// a full list drops the oldest
	WifiSaved small[2]; int sc = 0;
	sc = wifi_saved_put(small, sc, 2, "a", "1"); sc = wifi_saved_put(small, sc, 2, "b", "2"); sc = wifi_saved_put(small, sc, 2, "c", "3");
	CHECK(sc==2 && !strcmp(small[0].ssid, "c") && !strcmp(small[1].ssid, "b"), "full: the oldest makes room");
	char longname[100]; memset(longname, 'x', 60); longname[60] = 0;
	CHECK(wifi_saved_put(small, sc, 2, longname, "p")==sc && !strcmp(small[0].ssid, "c"), "a name over 32 bytes is refused");
	CHECK(wifi_saved_store("/no/such/dir/x.txt", list, count)==-1, "storing somewhere impossible fails");
	// a hand-edited file: comments, blank lines, CRLF, a name with no password
	put(path, "# note\n\nLine One\tsecret\r\nOpenNet\nBadTooLong0123456789012345678901234567\tx\n");
	c2 = wifi_saved_load(path, back, WIFI_MAX_NETS);
	CHECK(c2==2 && !strcmp(back[0].ssid, "Line One") && !strcmp(back[0].pass, "secret") && !strcmp(back[1].ssid, "OpenNet") && !back[1].pass[0], "comments, blanks, CRLF and name-only lines; an over-long name is skipped");

	// running commands
	snprintf(path, sizeof(path), "%s/bin", dir);
	mkdir(path, 0777);
	char cmd[700];
	snprintf(cmd, sizeof(cmd), "%s/hello", path); put(cmd, "#!/bin/sh\necho \"hello $1\"\nexit 3\n");
	snprintf(cmd, sizeof(cmd), "%s/slow", path); put(cmd, "#!/bin/sh\nsleep 5\necho late\n");
	snprintf(cmd, sizeof(cmd), "%s/big", path); put(cmd, "#!/bin/sh\nyes aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa | head -c 100000\n");
	char pathenv[1400]; snprintf(pathenv, sizeof(pathenv), "%s:%s", path, getenv("PATH"));
	setenv("PATH", pathenv, 1);
	char* a1[] = { "hello", "a b'c\"d;$x", NULL };
	int rc = wifi_run(a1, out, sizeof(out), 5000);
	CHECK(rc==3 && !strcmp(out, "hello a b'c\"d;$x\n"), "an argument with quotes, spaces and shell characters arrives untouched; exit status and output come back");
	char* a2[] = { "slow", NULL };
	CHECK(wifi_run(a2, out, sizeof(out), 500)==-2 && !out[0], "a command that takes too long is stopped (-2)");
	char* a3[] = { "no-such-command-here", NULL };
	CHECK(wifi_run(a3, out, sizeof(out), 2000)==127, "a missing command: 127");
	char* a4[] = { "big", NULL };
	CHECK(wifi_run(a4, out, 100, 5000)==0 && strlen(out)==99, "output is cut to the buffer, and the command still finishes");

	pid_t pid = wifi_spawn(a2);
	int st = 0;
	CHECK(pid>0 && !wifi_reap(pid, &st), "a spawned command is running");
	wifi_kill(pid);
	for (int i=0; i<50 && !wifi_reap(pid, &st); i++) usleep(50000);
	CHECK(wifi_reap(pid, &st), "...and stops when killed");
	pid_t quick = wifi_spawn((char*[]){ "hello", "x", NULL });
	for (int i=0; i<50 && !wifi_reap(quick, &st); i++) usleep(50000);
	CHECK(st==3, "its exit status is reported");

	// address and radio state, with fake commands and a fake /sys/class/net
	snprintf(cmd, sizeof(cmd), "%s/ip", path);
	put(cmd, "#!/bin/sh\nif [ \"$1\" = route ]; then echo 'default via 192.168.1.1 dev wlan0'; else echo '3: wlan0    inet 192.168.1.84/24 brd 192.168.1.255 scope global wlan0'; fi\n");
	wifi_ip_address(out, sizeof(out));
	CHECK(!strcmp(out, "192.168.1.84"), "the address from `ip`");
	CHECK(wifi_has_route(), "a default route is a way out");
	snprintf(cmd, sizeof(cmd), "%s/ip", path); put(cmd, "#!/bin/sh\nexit 0\n");
	wifi_ip_address(out, sizeof(out));
	CHECK(!out[0] && !wifi_has_route(), "no output: no address, no route");
	char net[700]; snprintf(net, sizeof(net), "%s/net/wlan0", dir);
	snprintf(cmd, sizeof(cmd), "%s/net", dir); mkdir(cmd, 0777); mkdir(net, 0777);
	snprintf(cmd, sizeof(cmd), "%s/flags", net); put(cmd, "0x1003\n");
	snprintf(cmd, sizeof(cmd), "%s/operstate", net); put(cmd, "down\n");
	snprintf(cmd, sizeof(cmd), "%s/net", dir); setenv("WIFI_NET_DIR", cmd, 1);
	CHECK(wifi_radio_on() && !wifi_radio_up(), "radio on (interface up) but no link");
	snprintf(cmd, sizeof(cmd), "%s/flags", net); put(cmd, "0x1002\n");
	snprintf(cmd, sizeof(cmd), "%s/operstate", net); put(cmd, "up\n");
	CHECK(!wifi_radio_on() && wifi_radio_up(), "interface down: radio off");
	return fails ? 1 : 0;
}
