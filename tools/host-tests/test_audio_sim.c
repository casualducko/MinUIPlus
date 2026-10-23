// Simulates the emulator feeding audio to the SDL callback with the real code from api.c:
// the callback pulls SAMPLES frames at the device rate while the emulator, paced by the
// video, pushes whatever one frame of the core's audio is. When the pacing is a little
// slower than the core's own frame rate (NES: 60.0988 fps core, paced at 60.0) the buffer
// slowly drains and the callback runs dry every few calls. That is the crackle.
//
// usage: test_audio_sim            run the checks
//        test_audio_sim report     print a table for a range of rates
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
typedef struct { int16_t left, right; } SND_Frame;
#define MIN(a,b) ((a)<(b)?(a):(b))
#define LOG_info(...)
static void SDL_LockAudio(void) {}
static void SDL_UnlockAudio(void) {}
static void SDL_Delay(unsigned ms);
#include "gen/SND_core.inc"

static double now_us, next_cb_us;
static const double cb_period_us = 1e6 * SAMPLES / 48000.0;
static void advance(double dt_us) {
	double target = now_us + dt_us;
	while (next_cb_us <= target) {
		int16_t out[SAMPLES*2];
		now_us = next_cb_us;
		SND_audioCallback(NULL, (uint8_t*)out, sizeof(out));
		next_cb_us += cb_period_us;
	}
	now_us = target;
}
static void SDL_Delay(unsigned ms) { advance(ms*1000.0); }

typedef struct { unsigned callbacks, starved, underruns, short_frames; double avg_fill; double pitch; } Result;

// core_fps: what the core says it runs at (audio per frame = 48000/core_fps)
// pace_fps: how fast the frontend really runs frames
static Result simulate(double core_fps, double pace_fps, double seconds) {
	memset(&snd, 0, sizeof(snd));
	snd.frame_rate = core_fps;
	snd.sample_rate_in = snd.sample_rate_out = 48000;
	snd.buffer_seconds = 5;
	SND_selectResampler();
	SND_resizeBuffer();
	snd.initialized = 1;
	now_us = 0; next_cb_us = cb_period_us;

	double per_frame = 48000.0 / core_fps, owed = 0, t = 0, frame_us = 1e6 / pace_fps;
	long frames = 0; double fill_sum = 0; long fill_n = 0;
	SND_Frame* chunk = calloc(2000, sizeof(SND_Frame));
	while (t < seconds*1e6) {
		advance(t - now_us > 0 ? t - now_us : 0);
		owed += per_frame; int n = (int)owed; owed -= n;
		for (int i=0;i<n;i++) { chunk[i].left = (int16_t)(8000*sin(frames*0.05)); chunk[i].right = chunk[i].left; frames++; }
		int fill = snd.frame_in - snd.frame_out; if (fill<0) fill += snd.frame_count;
		fill_sum += fill; fill_n++;
		SND_batchSamples(chunk, n);
		t += frame_us;
	}
	free(chunk);
	// frames played per frame the core made: 1.0 is untouched, 1.002 is the audio stretched 0.2%
	Result r = { snd.callbacks, snd.starved, snd.underruns, snd.underrun_frames, fill_sum/fill_n, (double)snd.callbacks*SAMPLES/frames };
	return r;
}

static int fails = 0;
#define CHECK(c,m) do { int ok=(c); printf("%s  %s\n", ok?"ok  ":"FAIL", m); if(!ok) fails++; } while(0)
int main(int argc, char** argv) {
	if (argc>1 && !strcmp(argv[1],"report")) {
		printf("core fps  paced at  underruns/callbacks   frames short   avg fill  speed\n");
		double pairs[][2] = {{60.0988,60.0},{60.0988,59.87},{60.0988,60.2},{59.7275,60.0},{50.0,50.0},{60.0988,60.0988}};
		for (int i=0;i<6;i++) { Result r = simulate(pairs[i][0],pairs[i][1],60);
			printf("%8.4f  %8.4f   %6u / %-6u (%4.1f%%)  %8u   %6.0f   %.4f\n", pairs[i][0], pairs[i][1], r.underruns, r.callbacks, 100.0*r.underruns/r.callbacks, r.short_frames, r.avg_fill, r.pitch); }
		return 0;
	}
	// what your device logged for NES: 8465 callbacks, 169 starved, 2759 underruns, 5468 frames short
	Result r = simulate(60.0988, 60.0, 70.5);
	printf("      NES (core 60.0988, paced 60.0): %u callbacks, %u starved, %u underruns, %u frames short\n", r.callbacks, r.starved, r.underruns, r.short_frames);
	CHECK(r.underruns < r.callbacks/200, "NES: underruns are rare (a few per thousand callbacks at most)");
	CHECK(r.short_frames < 400, "NES: very few frames of audio are missing");
	CHECK(fabs(r.pitch-1.0) < 0.005, "NES: the audio is stretched by well under 0.5%");
	CHECK(r.avg_fill < 2000, "NES: the buffer stays shallow (under ~40ms of added latency)");

	r = simulate(59.7275, 60.0, 60);
	CHECK(r.underruns < 10, "Game Boy (core slower than the pacing): still no underruns");

	r = simulate(60.0988, 59.87, 60);
	CHECK(r.underruns < r.callbacks/200, "NES paced at the panel's 59.87 Hz: still fine");
	CHECK(fabs(r.pitch-1.0) < 0.01, "NES at 59.87 Hz: stretched by under 1%");

	r = simulate(60.0988, 60.0988, 60);
	CHECK(r.underruns < 10, "pacing exactly matches the core: fine");
	return fails!=0;
}
