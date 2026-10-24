#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <errno.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <string.h>
#include <linux/input.h>

#include "msettings.h"

///////////////////////////////////////

#define SETTINGS_VERSION 2
typedef struct Settings {
	int version; // future proofing
	int brightness;
	int headphones;
	int speaker;
	int unused[2]; // for future use
	// NOTE: doesn't really need to be persisted but still needs to be shared
	int jack; 
	int hdmi; 
} Settings;
static Settings DefaultSettings = {
	.version = SETTINGS_VERSION,
	.brightness = 2,
	.headphones = 4,
	.speaker = 8,
	.jack = 0,
	.hdmi = 0,
};
static Settings* settings;

static int clampInt(int value, int min, int max) {
	return value<min ? min : (value>max ? max : value);
}
// the file is rewritten in place, so a power cut or a bad card can leave anything in it,
// and a volume of 9999 would otherwise be applied as `amixer sset 'Master' 49995%`
static void SanitizeSettings(Settings* s) {
	s->brightness = clampInt(s->brightness, 0, 10);
	s->headphones = clampInt(s->headphones, 0, 20);
	s->speaker = clampInt(s->speaker, 0, 20);
	s->jack = s->jack ? 1 : 0;
	s->hdmi = s->hdmi ? 1 : 0;
}

#define SHM_KEY "/SharedSettings"
static char SettingsPath[256];
static int shm_fd = -1;
static int is_host = 0;
static int shm_size = sizeof(Settings);

#define BACKLIGHT_PATH "/sys/class/backlight/backlight/bl_power"
#define BRIGHTNESS_PATH "/sys/class/backlight/backlight/brightness"
#define JACK_STATE_PATH "/sys/bus/platform/devices/singleadc-joypad/hp"
#define HDMI_STATE_PATH "/sys/class/extcon/hdmi/cable.0/state"

int getInt(char* path) {
	int i = 0;
	FILE *file = fopen(path, "r");
	if (file!=NULL) {
		fscanf(file, "%i", &i);
		fclose(file);
	}
	return i;
}


void InitSettings(void) {	
	sprintf(SettingsPath, "%s/msettings.bin", getenv("USERDATA_PATH"));
	
	shm_fd = shm_open(SHM_KEY, O_RDWR | O_CREAT | O_EXCL, 0644); // see if it exists
	if (shm_fd==-1 && errno==EEXIST) { // already exists
		puts("Settings client");
		shm_fd = shm_open(SHM_KEY, O_RDWR, 0644);
		settings = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	}
	else { // host
		puts("Settings host"); // should always be keymon
		is_host = 1;
		// we created it so set initial size and populate
		ftruncate(shm_fd, shm_size);
		settings = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
		
		int fd = open(SettingsPath, O_RDONLY);
		if (fd>=0) {
			ssize_t got = read(fd, settings, shm_size);
			// TODO: use settings->version for future proofing?
			close(fd);
			if (got!=shm_size) memcpy(settings, &DefaultSettings, shm_size); // cut short, don't trust half of it
			SanitizeSettings(settings);
		}
		else {
			// load defaults
			memcpy(settings, &DefaultSettings, shm_size);
		}
		
		// these shouldn't be persisted
		// settings->jack = 0;
		// settings->hdmi = 0;
	}
	
	int jack = getInt(JACK_STATE_PATH);
	int hdmi = getInt(HDMI_STATE_PATH);
	printf("brightness: %i (hdmi: %i)\nspeaker: %i (jack: %i)\n", settings->brightness, hdmi, settings->speaker, jack); fflush(stdout);
	
	// both of these set volume
	SetJack(jack);
	SetHDMI(hdmi);
	
	SetBrightness(GetBrightness());
	// system("echo $(< " BRIGHTNESS_PATH ")");
}
void QuitSettings(void) {
	munmap(settings, shm_size);
	if (is_host) shm_unlink(SHM_KEY);
}
static inline void SaveSettings(void) {
	int fd = open(SettingsPath, O_CREAT|O_WRONLY, 0644);
	if (fd>=0) {
		write(fd, settings, shm_size);
		close(fd);
		sync();
	}
}

int GetBrightness(void) { // 0-10
	return settings->brightness;
}
void SetBrightness(int value) {
	if (settings->hdmi) return;
	
	int raw;
	switch (value) {
		// TODO: redo, range is 0-255
		case 0: raw=4; break; 		//  0
		case 1: raw=6; break; 		//  2
		case 2: raw=10; break; 		//  4
		case 3: raw=16; break; 		//  6
		case 4: raw=32; break;		// 16
		case 5: raw=48; break;		// 16
		case 6: raw=64; break;		// 16
		case 7: raw=96; break;		// 32
		case 8: raw=128; break;		// 32
		case 9: raw=192; break;		// 64
		case 10: raw=255; break;	// 64
	}
	SetRawBrightness(raw);
	settings->brightness = value;
	SaveSettings();
}

int GetVolume(void) { // 0-20
	return settings->jack ? settings->headphones : settings->speaker;
}
void SetVolume(int value) {
	if (settings->hdmi) return;
	
	if (settings->jack) settings->headphones = value;
	else settings->speaker = value;
	
	int raw = value * 5;
	SetRawVolume(raw);
	SaveSettings();
}

void SetRawBrightness(int val) { // 0 - 255
	if (settings->hdmi) return;
	
	// printf("SetRawBrightness(%i)\n", val); fflush(stdout);
	int fd = open(BRIGHTNESS_PATH, O_WRONLY);
	if (fd>=0) {
		dprintf(fd,"%d",val);
		close(fd);
	}
}
void SetRawVolume(int val) { // 0 - 100
	// printf("SetRawVolume(%i)\n", val); fflush(stdout);
	char cmd[256];
	sprintf(cmd, "amixer sset -M 'Master' %i%% &> /dev/null", val);
	// puts(cmd); fflush(stdout);
	system(cmd);
}

// monitored and set by thread in keymon
int GetJack(void) {
	return settings->jack;
}
void SetJack(int value) {
	// printf("SetJack(%i)\n", value); fflush(stdout);
	
	char cmd[256];
	sprintf(cmd, "amixer cset name='Playback Path' '%s' &> /dev/null", value?"HP":"SPK");
	system(cmd);
	
	settings->jack = value;
	SetVolume(GetVolume());
}

int GetHDMI(void) {	
	// printf("GetHDMI() %i\n", settings->hdmi); fflush(stdout);
	return settings->hdmi;
}
void SetHDMI(int value) {
	// printf("SetHDMI(%i)\n", value); fflush(stdout);
	
	// if (settings->hdmi!=value) system("/usr/lib/autostart/common/055-hdmi-check");
	
	settings->hdmi = value;
	if (value) SetRawVolume(100); // max
	else SetVolume(GetVolume()); // restore
}

int GetMute(void) { return 0; }
void SetMute(int value) { }

///////////////////////////////////////
// Rumble. The joypad driver registers a force-feedback device (FF_RUMBLE, which drives the PWM motor):
// that is used when there is one. If not, the PWM that Moss sets up at boot (pwmchip1/pwm0, period 1 ms,
// duty = period meaning off) is driven directly, but only if it is already set up that way, so nothing
// else's PWM (the backlight's) can be touched by a wrong guess.

#define RUMBLE_PWM "/sys/class/pwm/pwmchip1/pwm0"
static int rumble_state = -1; // -1 not looked at yet, 0 nothing to drive, 1 force feedback, 2 sysfs PWM
static int rumble_fd = -1;
static int rumble_id = -1;
static int rumble_period = 0;

static int RumbleHasBit(const unsigned long* bits, int bit) {
	return (bits[bit / (8 * sizeof(unsigned long))] >> (bit % (8 * sizeof(unsigned long)))) & 1;
}
static int RumbleReadInt(const char* path) {
	int value = -1;
	FILE* f = fopen(path, "r");
	if (!f) return -1;
	if (fscanf(f, "%d", &value)!=1) value = -1;
	fclose(f);
	return value;
}
static void RumbleOpen(void) {
	rumble_state = 0;
	for (int i=0; i<16; i++) {
		char path[32];
		snprintf(path, sizeof(path), "/dev/input/event%i", i);
		int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
		if (fd<0) continue;
		unsigned long types[(EV_MAX / (8 * sizeof(unsigned long))) + 1];
		unsigned long effects[(FF_MAX / (8 * sizeof(unsigned long))) + 1];
		memset(types, 0, sizeof(types));
		memset(effects, 0, sizeof(effects));
		if (ioctl(fd, EVIOCGBIT(0, sizeof(types)), types)>=0 && RumbleHasBit(types, EV_FF) &&
			ioctl(fd, EVIOCGBIT(EV_FF, sizeof(effects)), effects)>=0 && RumbleHasBit(effects, FF_RUMBLE)) {
			rumble_fd = fd;
			rumble_state = 1;
			return;
		}
		close(fd);
	}
	rumble_period = RumbleReadInt(RUMBLE_PWM "/period");
	if (rumble_period==1000000 && RumbleReadInt(RUMBLE_PWM "/enable")==1) rumble_state = 2;
}
int GetRumbleBackend(void) { // 0 none, 1 force feedback, 2 PWM (after the first SetRawRumble)
	return rumble_state<0 ? 0 : rumble_state;
}
void SetRawRumble(int strength) { // 0-65535, 0 is off
	if (rumble_state<0) RumbleOpen();
	if (strength<0) strength = 0;
	if (strength>0xffff) strength = 0xffff;
	if (rumble_state==1) {
		struct input_event play;
		memset(&play, 0, sizeof(play));
		play.type = EV_FF;
		if (strength==0) {
			if (rumble_id<0) return;
			play.code = (unsigned short)rumble_id;
			play.value = 0;
			if (write(rumble_fd, &play, sizeof(play))<0) { /* nothing more to do */ }
			return;
		}
		struct ff_effect effect;
		memset(&effect, 0, sizeof(effect));
		effect.type = FF_RUMBLE;
		effect.id = (short)rumble_id; // -1 the first time: the driver picks one
		effect.replay.length = 60000; // restarted whenever the strength changes, stopped with a 0
		effect.u.rumble.strong_magnitude = (unsigned short)strength;
		effect.u.rumble.weak_magnitude = 0;
		if (ioctl(rumble_fd, EVIOCSFF, &effect)<0) return;
		rumble_id = effect.id;
		play.code = (unsigned short)rumble_id;
		play.value = 1;
		if (write(rumble_fd, &play, sizeof(play))<0) { /* nothing more to do */ }
	}
	else if (rumble_state==2) {
		// Moss's "off" is duty == period, so more strength means a shorter duty
		int duty = rumble_period - (int)((long long)rumble_period * strength / 0xffff);
		char value[16];
		snprintf(value, sizeof(value), "%d", duty);
		int fd = open(RUMBLE_PWM "/duty_cycle", O_WRONLY);
		if (fd>=0) {
			if (write(fd, value, strlen(value))<0) { /* nothing more to do */ }
			close(fd);
		}
	}
}
