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

static int sim_target_ms;   // the audio-buffer setting (0: default)
static double sim_spike_ms;  // a busy moment: one frame takes this much longer...
static double sim_spike_every_s; // ...this often
static int single_frame_calls; // some cores push one frame at a time (audio_sample_callback)
typedef struct { unsigned callbacks, starved, underruns, short_frames; double avg_fill; double pitch; } Result;

// core_fps: what the core says it runs at (audio per frame = 48000/core_fps)
// pace_fps: how fast the frontend really runs frames
static Result simulate(double core_fps, double pace_fps, double seconds) {
	memset(&snd, 0, sizeof(snd));
	snd.frame_rate = core_fps;
	snd.sample_rate_in = snd.sample_rate_out = 48000;
	snd.buffer_seconds = 5;
	snd.target_ms = sim_target_ms;
	snd.drc_enabled = 1; // what SND_init does unless the no-audio-rate-control file exists
	snd.drc_rate_min = snd.drc_rate_max = snd.sample_rate_in;
	SND_selectResampler();
	SND_resizeBuffer();
	snd.initialized = 1;
	now_us = 0; next_cb_us = cb_period_us;

	double per_frame = 48000.0 / core_fps, owed = 0, t = 0, frame_us = 1e6 / pace_fps;
	long frames = 0; double fill_sum = 0; long fill_n = 0; double next_spike = sim_spike_every_s*1e6;
	SND_Frame* chunk = calloc(2000, sizeof(SND_Frame));
	while (t < seconds*1e6) {
		advance(t - now_us > 0 ? t - now_us : 0);
		owed += per_frame; int n = (int)owed; owed -= n;
		for (int i=0;i<n;i++) { chunk[i].left = (int16_t)(8000*sin(frames*0.05)); chunk[i].right = chunk[i].left; frames++; }
		int fill = snd.frame_in - snd.frame_out; if (fill<0) fill += snd.frame_count;
		fill_sum += fill; fill_n++;
		if (getenv("SIM_TRACE") && fill_n%60==0) printf("  t=%5.1fs fill=%5d rate_in=%d\n", t/1e6, fill, snd.drc_rate_in);
		if (single_frame_calls) for (int i=0;i<n;i++) SND_batchSamples(&chunk[i], 1);
		else SND_batchSamples(chunk, n);
		t += frame_us;
		if (sim_spike_ms>0 && t>=next_spike) { t += sim_spike_ms*1000; next_spike += sim_spike_every_s*1e6; }
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
		for (int mode=0; mode<2; mode++) {
		single_frame_calls = mode;
		printf("%s\ncore fps  paced at  underruns/callbacks   frames short   avg fill  speed\n", mode ? "one frame per call:" : "whole batches:");
		double pairs[][2] = {{60.0988,60.0},{60.0988,59.87},{60.0988,60.2},{59.7275,60.0},{50.0,50.0},{60.0988,60.0988}};
		for (int i=0;i<6;i++) { Result r = simulate(pairs[i][0],pairs[i][1],60);
			printf("%8.4f  %8.4f   %6u / %-6u (%4.1f%%)  %8u   %6.0f   %.4f\n", pairs[i][0], pairs[i][1], r.underruns, r.callbacks, 100.0*r.underruns/r.callbacks, r.short_frames, r.avg_fill, r.pitch); }
		}
		return 0;
	}
	// what your device logged for NES: 8465 callbacks, 169 starved, 2759 underruns, 5468 frames short
	Result r = simulate(60.0988, 60.0, 70.5);
	printf("      NES (core 60.0988, paced 60.0): %u callbacks, %u starved, %u underruns, %u frames short\n", r.callbacks, r.starved, r.underruns, r.short_frames);
	CHECK(r.underruns < r.callbacks/200, "NES: underruns are rare (a few per thousand callbacks at most)");
	CHECK(r.short_frames < 400, "NES: very few frames of audio are missing");
	CHECK(fabs(r.pitch-1.0) < 0.005, "NES: the audio is stretched by well under 0.5%");
	CHECK(r.avg_fill < 2000, "NES: the buffer stays shallow (under ~40ms of added latency)");

	single_frame_calls = 1;
	r = simulate(60.0988, 60.0, 60);
	CHECK(r.underruns < r.callbacks/200 && fabs(r.pitch-1.0) < 0.005, "NES with a core that pushes one frame per call: no underruns, small stretch");
	single_frame_calls = 0;

	r = simulate(59.7275, 60.0, 60);
	CHECK(r.underruns < 10, "Game Boy (core slower than the pacing): still no underruns");

	r = simulate(60.0988, 59.87, 60);
	CHECK(r.underruns < r.callbacks/200, "NES paced at the panel's 59.87 Hz: still fine");
	CHECK(fabs(r.pitch-1.0) < 0.01, "NES at 59.87 Hz: stretched by under 1%");

	r = simulate(60.0988, 60.0988, 60);
	CHECK(r.underruns < 10, "pacing exactly matches the core: fine");

	// busy moments (the Zelda case): now and then one frame takes much longer to make
	sim_spike_ms = 45; sim_spike_every_s = 15; // the rate control can only win back about 1% of the time, so these have to be occasional
	r = simulate(60.0988, 60.0, 60);
	printf("      45ms busy frame every 15s, default cushion: %u starved? underruns %u of %u, %u frames short\n", snd.starved, r.underruns, r.callbacks, r.short_frames);
	unsigned base_short = r.short_frames;
	CHECK(r.underruns > 0, "default cushion: a 45ms busy frame does run the buffer dry (the problem)");
	sim_target_ms = 80;
	r = simulate(60.0988, 60.0, 60);
	printf("      same, audio-buffer 80:   underruns %u of %u, %u frames short, avg fill %.0f\n", r.underruns, r.callbacks, r.short_frames, r.avg_fill);
	CHECK(r.underruns==0 && r.short_frames==0, "audio-buffer 80: the same busy frames are covered, nothing is dropped");
	CHECK(r.short_frames < base_short, "less audio is missing than with the default");
	sim_target_ms = 80; sim_spike_ms = 0;
	r = simulate(60.0988, 60.0, 60);
	CHECK(r.underruns==0 && fabs(r.pitch-1.0) < 0.005, "audio-buffer 80 without busy frames: still clean, stretch under 0.5%");
	CHECK(r.avg_fill > 3000 && r.avg_fill < 5000, "audio-buffer 80: the cushion is about 80ms (3840 frames at 48kHz)");
	sim_target_ms = 0;
	return fails!=0;
}
