// SND_audioCallback() from api.c: how it fills the buffer when the emulator falls behind
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

// a ring of 64 frames with `available` unread frames starting at 60 (so it wraps)
static void setup(int available) {
	static SND_Frame buf[64];
	for (int i=0;i<64;i++) { buf[i].left = 1000+i*10; buf[i].right = -(2000+i*10); }
	snd.buffer = buf; snd.frame_count = 64; snd.frame_out = 60; snd.frame_in = (60+available)%64; snd.frame_filled = 59;
	snd.callbacks = snd.starved = snd.underruns = snd.underrun_frames = 0;
}
static int fails = 0;
#define CHECK(c, msg) do { int ok = (c); printf("%s  %s\n", ok?"ok  ":"FAIL", msg); if (!ok) fails++; } while(0)

int main(void) {
	int16_t out[SAMPLES*2];

	setup(SAMPLES); SND_audioCallback(NULL,(uint8_t*)out,sizeof(out));
	CHECK(snd.underruns==0 && snd.starved==0 && snd.callbacks==1, "enough frames: nothing counted");
	CHECK(out[0]==snd.buffer[60].left && out[1]==snd.buffer[60].right, "enough frames: first frame is the first buffered frame");

	setup(0); memset(out,0x55,sizeof(out)); SND_audioCallback(NULL,(uint8_t*)out,sizeof(out));
	int allzero=1; for (int i=0;i<SAMPLES*2;i++) if (out[i]) allzero=0;
	CHECK(allzero, "empty buffer: silence");
	CHECK(snd.starved==1 && snd.underruns==0, "empty buffer: counted as starved, not as an underrun");

	// 3 real frames then 5 missing: the missing ones mirror the real ones, left with left and right with right
	setup(3); memset(out,0x55,sizeof(out)); SND_audioCallback(NULL,(uint8_t*)out,sizeof(out));
	CHECK(snd.underruns==1 && snd.underrun_frames==5, "partial underrun: 1 counted, 5 frames short");
	int copied=1; for (int f=0; f<3; f++) copied &= out[f*2]==snd.buffer[60+f].left && out[f*2+1]==snd.buffer[60+f].right;
	CHECK(copied, "partial underrun: real frames copied intact");
	int exp[5] = {2,1,0,-1,-1}, mirrored=1;
	for (int i=0;i<5;i++) {
		int16_t el = exp[i]>=0 ? out[exp[i]*2]   : 0;
		int16_t er = exp[i]>=0 ? out[exp[i]*2+1] : 0;
		mirrored &= out[(3+i)*2]==el && out[(3+i)*2+1]==er;
	}
	CHECK(mirrored, "partial underrun: mirrored frames keep left and right together, then silence");
	return fails!=0;
}
