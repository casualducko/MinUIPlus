// Setting the clock from the network (see HTTPDate_parse): what the Clock tool's Y button does.
#ifndef CLOCKNET_H
#define CLOCKNET_H
#include <time.h>

// Called about every 50 ms while something is going on: shows `message`, returns 1 if the user cancelled.
typedef int (*ClockNet_tick)(void* ctx, const char* message);

// Brings Wi-Fi up if there is no route (wifictl enable), then reads the time from the Date: header of
// a small plain-http answer. 0 and the time in *now; or -1 and a message for the user in *error.
// Tests: CLOCK_URL (the only address tried), CLOCK_ROUTE_FILE, CLOCK_WIFICTL, CLOCK_WIFI_TIMEOUT_MS.
int ClockNet_sync(time_t* now, const char** error, ClockNet_tick tick, void* ctx);

#endif
