// SND_fadeIn() from api.c: the first moments of sound ramp up from silence
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define SAMPLES 8
typedef struct { int16_t left, right; } SND_Frame;
static struct {
	size_t frame_count; int frame_in, frame_out, frame_filled;
	SND_Frame* buffer;
	unsigned callbacks, starved, underruns, underrun_frames;
	int peak_out;
	int fade_pos, fade_len;
} snd;
#define LOG_info(...)
#include "gen/SND_audioCallback.inc"

static SND_Frame ring[64];
static void feed(int n, int16_t l, int16_t r) { // n frames of a constant level
	for (int i=0;i<n;i++) { ring[snd.frame_in].left = l; ring[snd.frame_in].right = r; snd.frame_in = (snd.frame_in+1)%64; }
}
static int fails = 0;
#define CHECK(c, msg) do { int sane = (c); printf("%s  %s\n", sane?"ok  ":"FAIL", msg); if (!sane) fails++; } while(0)

int main(void) {
	int16_t out[SAMPLES*2];
	snd.buffer = ring; snd.frame_count = 64; snd.fade_len = 20; // 2.5 callbacks

	// silence first: the fade must not start until sound does
	SND_audioCallback(NULL,(uint8_t*)out,sizeof(out));
	CHECK(snd.fade_pos==0, "empty buffer: fade has not started");
	feed(SAMPLES, 0, 0); SND_audioCallback(NULL,(uint8_t*)out,sizeof(out));
	CHECK(snd.fade_pos==0, "silent frames: fade has not started");

	// a level that is already high on the first frame: starts at 0 and climbs
	feed(40, 10000, -20000);
	SND_audioCallback(NULL,(uint8_t*)out,sizeof(out));
	CHECK(out[0]==0 && out[1]==0, "first frame of sound is silent");
	int rising = 1; for (int i=1;i<SAMPLES;i++) if (out[i*2] <= out[(i-1)*2] || out[i*2+1] >= out[(i-1)*2+1]) rising = 0;
	CHECK(rising, "level climbs frame by frame, both channels (right is negative)");
	CHECK(out[2]==500 && out[3]==-1000, "linear: 1/20 of the level after one frame");
	SND_audioCallback(NULL,(uint8_t*)out,sizeof(out));
	SND_audioCallback(NULL,(uint8_t*)out,sizeof(out)); // 24 frames: fade (20) ended inside this one
	CHECK(snd.fade_pos==20, "fade stops at its length");
	CHECK(out[2*4]==10000 && out[2*4+1]==-20000 && out[2*7]==10000, "full level after the fade");
	SND_audioCallback(NULL,(uint8_t*)out,sizeof(out));
	CHECK(out[0]==10000, "later callbacks are untouched");

	// a callback with fade_len 0 (no fade configured) changes nothing
	snd.fade_pos = 0; snd.fade_len = 0; snd.frame_out = snd.frame_in; feed(SAMPLES, 7, 7);
	SND_audioCallback(NULL,(uint8_t*)out,sizeof(out));
	CHECK(out[0]==7 && out[1]==7, "fade_len 0: no fade");

	printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
	return fails != 0;
}
