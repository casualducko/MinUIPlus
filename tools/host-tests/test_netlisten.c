// Net_listening() in api.c: finding sshd in /proc/net/tcp text
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gen/netlisten.inc"
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)
int main(void) {
	const char* tcp =
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
		"   0: 00000000:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 12345 1 0000000000000000 100 0 0 10 0\n"
		"   1: 0100007F:1F90 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 12346 1 0000000000000000 100 0 0 10 0\n"
		"   2: 5401A8C0:0016 0B01A8C0:C350 01 00000000:00000000 00:00000000 00000000     0        0 12347 1 0000000000000000 20 4 30 10 -1\n";
	CHECK(Net_listening(tcp, 22), "port 22 listening");
	CHECK(Net_listening(tcp, 8080), "another port listening (0x1F90)");
	CHECK(!Net_listening(tcp, 80), "a port nobody listens on");
	CHECK(!Net_listening("  sl  local_address rem_address   st\n   0: 5401A8C0:0016 0B01A8C0:C350 01 0:0\n", 22), "an established connection to 22 is not listening");
	CHECK(Net_listening("   0: 00000000000000000000000000000000:0016 00000000000000000000000000000000:0000 0A 0:0\n", 22), "tcp6 addresses");
	// the real thing, as the device prints it (columns padded, a trailing run of spaces)
	const char* real =
		"  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode                                                     \n"
		"   0: 0100007F:1269 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 20245 1 000000008c52ba02 99 0 0 10 0                      \n"
		"   1: 00000000:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 23402 1 0000000099ed1768 99 0 0 10 0                      \n"
		"   2: 5401A8C0:0016 3701A8C0:FD33 01 00000024:00000000 01:0000003A 00000000     0        0 24053 4 000000008416612f 225 3 1 6 -1                     \n";
	CHECK(Net_listening(real, 22) && !Net_listening(real, 80), "text copied from the device");
	CHECK(!Net_listening("", 22) && !Net_listening("garbage\nmore garbage", 22), "empty and garbage");
	printf(fails ? "\n%d FAILED\n" : "\nall netlisten tests passed\n", fails);
	return fails ? 1 : 0;
}
