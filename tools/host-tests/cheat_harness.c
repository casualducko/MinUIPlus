// Runs a real libretro core, turns a cheat on through retro_cheat_set the way minarch does,
// and shows that the game's RAM changes. usage: harness core.so game ADDR CODE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dlfcn.h>
#include "libretro.h"
static bool env(unsigned cmd, void* data) {
	switch (cmd) {
		case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data = true; return true;
		case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
		case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data = "/tmp"; return true;
		case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: return false;
		default: return false;
	}
}
static void video(const void* d, unsigned w, unsigned h, size_t p) {}
static void audio1(int16_t l, int16_t r) {}
static size_t audio(const int16_t* d, size_t n) { return n; }
static void poll(void) {}
static int16_t input(unsigned a, unsigned b, unsigned c, unsigned d) { return 0; }
int main(int argc, char** argv) {
	void* h = dlopen(argv[1], RTLD_NOW);
	if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
	#define SYM(t, n) t n = dlsym(h, "retro_" #n)
	void (*set_env)(retro_environment_t) = dlsym(h, "retro_set_environment");
	void (*set_video)(retro_video_refresh_t) = dlsym(h, "retro_set_video_refresh");
	void (*set_audio)(retro_audio_sample_t) = dlsym(h, "retro_set_audio_sample");
	void (*set_batch)(retro_audio_sample_batch_t) = dlsym(h, "retro_set_audio_sample_batch");
	void (*set_poll)(retro_input_poll_t) = dlsym(h, "retro_set_input_poll");
	void (*set_input)(retro_input_state_t) = dlsym(h, "retro_set_input_state");
	void (*init)(void) = dlsym(h, "retro_init");
	bool (*load)(const struct retro_game_info*) = dlsym(h, "retro_load_game");
	void (*run)(void) = dlsym(h, "retro_run");
	void* (*mem)(unsigned) = dlsym(h, "retro_get_memory_data");
	void (*cheat_reset)(void) = dlsym(h, "retro_cheat_reset");
	void (*cheat_set)(unsigned, bool, const char*) = dlsym(h, "retro_cheat_set");
	set_env(env); set_video(video); set_audio(audio1); set_batch(audio); set_poll(poll); set_input(input);
	init();
	FILE* f = fopen(argv[2], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	void* data = malloc(n); fread(data, 1, n, f); fclose(f);
	struct retro_game_info g = { argv[2], data, n, NULL };
	if (!load(&g)) { printf("load failed\n"); return 1; }
	unsigned addr = strtoul(argv[3], NULL, 16);
	unsigned char* ram = mem(RETRO_MEMORY_SYSTEM_RAM);
	if (!ram) { printf("no system ram\n"); return 1; }
	for (int i=0; i<120; i++) run();
	printf("before: ram[%04X] = %02X\n", addr, ram[addr]);
	cheat_reset();
	cheat_set(0, true, argv[4]);
	for (int i=0; i<120; i++) run();
	printf("cheat on: ram[%04X] = %02X (code %s)\n", addr, ram[addr], argv[4]);
	cheat_reset();
	ram[addr] = 0;
	for (int i=0; i<120; i++) run();
	printf("after reset: ram[%04X] = %02X\n", addr, ram[addr]);
	return 0;
}
