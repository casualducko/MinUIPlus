#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include "utils.h"
#include "clocknet.h"

static uint64_t nowMs(void) { return getMicroseconds() / 1000; }

static int hasRoute(void) { // a default route, by whatever interface
	const char* e = getenv("CLOCK_ROUTE_FILE");
	FILE* f = fopen((e && *e) ? e : "/proc/net/route", "r");
	if (!f) return 1; // not the device: nothing to switch on
	char line[256];
	int found = 0;
	while (fgets(line, sizeof(line), f)) {
		char iface[32], dest[16];
		if (sscanf(line, "%31s %15s", iface, dest)==2 && !strcmp(dest, "00000000")) { found = 1; break; }
	}
	fclose(f);
	return found;
}
// runs argv with its standard output captured: 0 done, -1 couldn't start, -2 cancelled, -3 too slow
int ClockNet_capture(char* const argv[], char* out, size_t cap, int timeout_ms, ClockNet_tick tick, void* ctx, const char* message) {
	int fds[2];
	out[0] = '\0';
	if (pipe(fds)) return -1;
	pid_t pid = fork();
	if (pid<0) { close(fds[0]); close(fds[1]); return -1; }
	if (pid==0) {
		setpgid(0, 0); // its own group: a cancel stops what the shell started too (tar, sha256sum), not only the shell
		int nul = open("/dev/null", O_RDWR);
		if (nul>=0) { dup2(nul, 0); dup2(nul, 2); }
		dup2(fds[1], 1);
		for (int fd=3; fd<256; fd++) close(fd);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fds[1]);
	size_t len = 0;
	int result = 0;
	uint64_t start = nowMs();
	while (1) {
		if (tick(ctx, message)) { result = -2; break; }
		if (nowMs() - start > (uint64_t)timeout_ms) { result = -3; break; }
		struct pollfd p = { fds[0], POLLIN, 0 };
		int r = poll(&p, 1, 50);
		if (r>0) {
			ssize_t n = read(fds[0], out + len, cap - 1 - len);
			if (n<=0) break; // the program is done
			len += n;
			out[len] = '\0';
			if (len >= cap-1) break;
		}
	}
	close(fds[0]);
	if (result!=0) { kill(-pid, SIGKILL); kill(pid, SIGKILL); } // the group, and the child itself in case it had not made its own group yet
	waitpid(pid, NULL, 0);
	return result;
}
int ClockNet_wifiUp(const char** error, ClockNet_tick tick, void* ctx) {
	if (!hasRoute()) {
		const char* cmd = getenv("CLOCK_WIFICTL");
		if (!cmd || !*cmd) cmd = "wifiup"; // wifictl enable, then the other saved networks
		pid_t pid = fork();
		if (pid==0) {
			int nul = open("/dev/null", O_RDWR);
			if (nul>=0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
			for (int fd=3; fd<256; fd++) close(fd);
			execlp(cmd, cmd, "enable", (char*)NULL);
			_exit(127);
		}
		const char* t = getenv("CLOCK_WIFI_TIMEOUT_MS");
		uint64_t limit = (t && *t) ? (uint64_t)atoll(t) : 45000;
		uint64_t start = nowMs();
		int done = 0; // wifictl has exited (and was collected)
		while (!hasRoute()) {
			int cancelled = tick(ctx, "Turning on Wi-Fi...");
			int too_slow = nowMs() - start > limit;
			if (pid>0 && !done && waitpid(pid, NULL, WNOHANG)==pid) done = 1;
			if (cancelled || too_slow) {
				*error = cancelled ? "Cancelled." : "No network.\nSet up Wi-Fi in Tools > Wi-Fi.";
				if (pid>0 && !done) { kill(pid, SIGTERM); waitpid(pid, NULL, 0); } // stopped and collected: no zombie
				return -1;
			}
			usleep(50000);
		}
		// the route is up; wifictl may still be finishing: it is left to (collected here if it is already done, otherwise by init when the tool ends)
		if (pid>0 && !done) waitpid(pid, NULL, WNOHANG);
	}
	return 0;
}
int ClockNet_sync(time_t* now, const char** error, ClockNet_tick tick, void* ctx) {
	if (ClockNet_wifiUp(error, tick, ctx)) return -1;
	const char* env = getenv("CLOCK_URL");
	const char* urls[] = { env, "http://connectivitycheck.gstatic.com/generate_204", "http://www.msftconnecttest.com/connecttest.txt", "http://detectportal.firefox.com/success.txt", NULL };
	for (int i = (env && *env) ? 0 : 1; urls[i]; i++) {
		char headers[4096];
		char* argv[] = { "curl", "-sI", "--connect-timeout", "6", "--max-time", "10", (char*)urls[i], NULL };
		int r = ClockNet_capture(argv, headers, sizeof(headers), 14000, tick, ctx, "Asking the network for the time...");
		if (r==-2) { *error = "Cancelled."; return -1; }
		if (r==0) {
			time_t t = HTTPDate_parse(headers);
			if (t) { *now = t; return 0; }
		}
		if (env && *env) break; // a test address: no others
	}
	*error = "Couldn't get the time.\nCheck the Wi-Fi connection.";
	return -1;
}
