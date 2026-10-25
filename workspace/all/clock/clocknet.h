// Setting the clock from the network (see HTTPDate_parse): what the Clock tool's Y button does.
#ifndef CLOCKNET_H
#define CLOCKNET_H
#include <stddef.h>
#include <time.h>

// Called about every 50 ms while something is going on: shows `message`, returns 1 if the user cancelled.
typedef int (*ClockNet_tick)(void* ctx, const char* message);

// Brings Wi-Fi up if there is no route (wifictl enable), then reads the time from the Date: header of
// a small plain-http answer. 0 and the time in *now; or -1 and a message for the user in *error.
// Tests: CLOCK_URL (the only address tried), CLOCK_ROUTE_FILE, CLOCK_WIFICTL, CLOCK_WIFI_TIMEOUT_MS.
int ClockNet_sync(time_t* now, const char** error, ClockNet_tick tick, void* ctx);

// Brings Wi-Fi up if there is no route (wifictl enable). 0 when there is a route; -1 and a message for the user.
int ClockNet_wifiUp(const char** error, ClockNet_tick tick, void* ctx);

// Runs argv with its standard output captured into out (cap bytes), ticking meanwhile: 0 done (whatever the
// exit status), -1 couldn't start, -2 cancelled (tick returned 1), -3 took longer than timeout_ms.
int ClockNet_capture(char* const argv[], char* out, size_t cap, int timeout_ms, ClockNet_tick tick, void* ctx, const char* message);

#endif
