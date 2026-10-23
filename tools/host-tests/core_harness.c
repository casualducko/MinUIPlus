// A tiny libretro frontend that runs a REAL core through the REAL SND_* code from api.c,
// with the audio callback on its own thread like SDL's, and records what comes out.
// It exists because the synthetic tests (test_audio_sim.c) said the audio rate control
// was fine while the device went silent: this feeds the code what a core actually produces.
//
// Build it with the device toolchain and the device's flags, run it on aarch64 (see
// run_core_harness.sh). The SND code to test is picked with -DSND_INC="file.inc".
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include "libretro.h"

typedef struct { int16_t left, right; } SND_Frame;
#define MIN(a,b) ((a)<(b)?(a):(b))
#define LOG_info(...)
static pthread_mutex_t audio_lock = PTHREAD_MUTEX_INITIALIZER;
static void SDL_LockAudio(void) { pthread_mutex_lock(&audio_lock); }
static void SDL_UnlockAudio(void) { pthread_mutex_unlock(&audio_lock); }
static void SDL_Delay(unsigned ms) { struct timespec ts = { ms/1000, (ms%1000)*1000000L }; nanosleep(&ts, NULL); }
#include SND_INC

#define MAX_SECONDS 40
static int16_t* pcm; static size_t pcm_frames;
static volatile int running = 1;
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec*1e9 + t.tv_nsec; }
static void sleep_until(double t) { struct timespec ts = { (time_t)(t/1e9), (long)fmod(t,1e9) }; clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL); }

static void* audio_thread(void* arg) {
	double period = 1e9 * SAMPLES / 48000.0, next = now_ns() + period;
	int16_t out[SAMPLES*2];
	while (running) {
		sleep_until(next); next += period;
		pthread_mutex_lock(&audio_lock);
		SND_audioCallback(NULL, (uint8_t*)out, sizeof(out));
		pthread_mutex_unlock(&audio_lock);
		if (pcm_frames + SAMPLES < (size_t)MAX_SECONDS*48000) { memcpy(pcm + pcm_frames*2, out, sizeof(out)); pcm_frames += SAMPLES; }
	}
	return NULL;
}

static int single;
static size_t cb_audio_batch(const int16_t* data, size_t frames) {
	if (single) { for (size_t i=0;i<frames;i++) SND_batchSamples((const SND_Frame*)&data[i*2], 1); return frames; }
	return SND_batchSamples((const SND_Frame*)data, frames);
}
static void cb_audio(int16_t l, int16_t r) { SND_Frame f = {l,r}; SND_batchSamples(&f, 1); }
static void cb_video(const void* d, unsigned w, unsigned h, size_t p) {}
static void cb_poll(void) {}
static int frame_no;
static int16_t cb_input(unsigned port, unsigned dev, unsigned idx, unsigned id) {
	// tap START now and then so games get past their title screen
	if (port==0 && dev==RETRO_DEVICE_JOYPAD && id==RETRO_DEVICE_ID_JOYPAD_START) return (frame_no%240) > 230;
	return 0;
}
static bool cb_env(unsigned cmd, void* data) {
	switch (cmd) {
		case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
		case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data = true; return true;
		default: return false;
	}
}

int main(int argc, char** argv) {
	if (argc<4) { fprintf(stderr,"usage: harness core.so game seconds [pace_fps] [single]\n"); return 2; }
	double seconds = atof(argv[3]), pace_fps = argc>4 ? atof(argv[4]) : 60.0; single = argc>5 && atoi(argv[5]);
	void* h = dlopen(argv[1], RTLD_NOW); if (!h) { fprintf(stderr,"dlopen: %s\n", dlerror()); return 1; }
	void (*set_env)(retro_environment_t) = dlsym(h,"retro_set_environment");
	void (*set_video)(retro_video_refresh_t) = dlsym(h,"retro_set_video_refresh");
	void (*set_audio)(retro_audio_sample_t) = dlsym(h,"retro_set_audio_sample");
	void (*set_batch)(retro_audio_sample_batch_t) = dlsym(h,"retro_set_audio_sample_batch");
	void (*set_poll)(retro_input_poll_t) = dlsym(h,"retro_set_input_poll");
	void (*set_input)(retro_input_state_t) = dlsym(h,"retro_set_input_state");
	void (*init)(void) = dlsym(h,"retro_init");
	bool (*load)(const struct retro_game_info*) = dlsym(h,"retro_load_game");
	void (*get_av)(struct retro_system_av_info*) = dlsym(h,"retro_get_system_av_info");
	void (*run)(void) = dlsym(h,"retro_run");
	set_env(cb_env); init();
	FILE* f = fopen(argv[2],"rb"); fseek(f,0,SEEK_END); long n = ftell(f); fseek(f,0,SEEK_SET);
	void* rom = malloc(n); fread(rom,1,n,f); fclose(f);
	struct retro_game_info gi = { argv[2], rom, (size_t)n, NULL };
	if (!load(&gi)) { fprintf(stderr,"load failed\n"); return 1; }
	set_video(cb_video); set_audio(cb_audio); set_batch(cb_audio_batch); set_poll(cb_poll); set_input(cb_input);
	struct retro_system_av_info av; get_av(&av);
	fprintf(stderr, "core: %.4f fps, audio %.0f Hz; pacing the frontend at %.4f fps; %s calls\n", av.timing.fps, av.timing.sample_rate, pace_fps, single?"one-frame":"batch");

	// SND_init without SDL
	memset(&snd, 0, sizeof(snd));
	snd.frame_rate = av.timing.fps; snd.sample_rate_in = av.timing.sample_rate; snd.sample_rate_out = 48000; snd.buffer_seconds = 5;
	snd.drc_enabled = getenv("NO_DRC") ? 0 : 1; // SND_init: on unless the no-audio-rate-control file exists
	snd.drc_rate_min = snd.drc_rate_max = snd.sample_rate_in;
	SND_selectResampler(); SND_resizeBuffer(); snd.initialized = 1;

	pcm = calloc((size_t)MAX_SECONDS*48000*2, sizeof(int16_t));
	pthread_t th; pthread_create(&th, NULL, audio_thread, NULL);
	double t = now_ns(), frame_ns = 1e9/pace_fps;
	long frames = (long)(seconds*pace_fps);
	for (frame_no=0; frame_no<frames; frame_no++) { run(); t += frame_ns; sleep_until(t); }
	running = 0; pthread_join(th, NULL);

	printf("callbacks %u, starved %u, underruns %u (%u frames short)\n", snd.callbacks, snd.starved, snd.underruns, snd.underrun_frames);
	printf("second: rms of the left channel / count of silent frames\n");
	for (size_t s=0; (s+1)*48000 <= pcm_frames; s++) {
		double sum=0; int zero=0;
		for (size_t i=0;i<48000;i++) { int l = pcm[(s*48000+i)*2], r = pcm[(s*48000+i)*2+1]; sum += (double)l*l; if (!l && !r) zero++; }
		printf("%3zu: rms %7.1f   silent frames %5d/48000\n", s, sqrt(sum/48000), zero);
	}
	return 0;
}
