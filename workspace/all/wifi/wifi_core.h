// The Wi-Fi tool's logic, with no SDL in it so it can be tested on a computer: reading what the
// radio sees (`iw`), the networks remembered on the card, and running the system's commands.
#ifndef WIFI_CORE_H
#define WIFI_CORE_H
#include <stddef.h>
#include <sys/types.h>

#define WIFI_SSID_MAX 33	// 32 bytes and the end
#define WIFI_PASS_MAX 64	// 63 characters and the end (the longest WPA passphrase)
#define WIFI_MAX_NETS 64

typedef struct {
	char ssid[WIFI_SSID_MAX];
	int signal;		// dBm, usually -30 (very close) to -90 (barely there)
	int secured;	// needs a password
} WifiNet;

typedef struct {
	char ssid[WIFI_SSID_MAX];
	char pass[WIFI_PASS_MAX];
} WifiSaved;

// "\xe3\x81\x82" style escapes (what `iw` prints for anything that isn't plain ASCII) back to bytes
int wifi_ssid_unescape(const char* in, char* out, size_t cap);

// `iw dev wlan0 scan dump` output: one entry per name (the strongest), strongest first,
// hidden networks (no name) left out. Returns how many.
int wifi_parse_scan(const char* text, WifiNet* out, int max);

// `connmanctl services` output (a fallback when iw shows nothing): "*AO Name     wifi_<id>_managed_psk"
// per line. No signal strength there (-70 is filled in); secured unless the service id ends in _none.
int wifi_parse_services(const char* text, WifiNet* out, int max);

// `iw dev wlan0 link` output. Returns 1 and fills ssid and signal if connected, else 0.
int wifi_parse_link(const char* text, char* ssid, size_t cap, int* signal);

// 0 (none) to 4 (excellent), from dBm
int wifi_bars(int dbm);
const char* wifi_bars_label(int bars);

// the networks remembered on the card: ssid<TAB>password per line, \\ \t \n escaped
int wifi_saved_load(const char* path, WifiSaved* out, int max);
int wifi_saved_store(const char* path, const WifiSaved* list, int count); // atomic; 0 or -1
int wifi_saved_find(const WifiSaved* list, int count, const char* ssid); // index or -1
int wifi_saved_put(WifiSaved* list, int count, int max, const char* ssid, const char* pass); // add or update (moves it to the front); new count
int wifi_saved_remove(WifiSaved* list, int count, int index); // new count

// run a command (argv[0] is looked up in PATH), collecting up to cap-1 bytes of its output.
// Gives up after timeout_ms (0 for no limit). Returns the exit status, -1 if it couldn't run, -2 on timeout.
int wifi_run(char* const argv[], char* out, size_t cap, int timeout_ms);

// start a command in the background with no output; wifi_reap() says when it's done
pid_t wifi_spawn(char* const argv[]);
int wifi_reap(pid_t pid, int* status); // 1 once it has finished (status filled), 0 if still running
void wifi_kill(pid_t pid);

// `ip -4 -o addr show wlan0`: the address, "" if none
void wifi_ip_address(char* out, size_t cap);
// is there a way out (a default route, by any interface)?
int wifi_has_route(void);
// is the radio switched on (wlan0 is up as an interface), connected or not? and is there a link (connected)?
int wifi_radio_on(void);
int wifi_radio_up(void);
#endif
