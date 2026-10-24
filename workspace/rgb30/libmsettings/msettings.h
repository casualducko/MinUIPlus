#ifndef __msettings_h__
#define __msettings_h__

void InitSettings(void);
void QuitSettings(void);

int GetBrightness(void);
int GetVolume(void);

void SetRawBrightness(int value); // 0-1024
void SetRawVolume(int value); // 0-40

void SetBrightness(int value); // 0-10
void SetVolume(int value); // 0-20

int GetJack(void);
void SetJack(int value); // 0-1

int GetHDMI(void);
void SetHDMI(int value); // 0-1

void SetRawRumble(int strength); // 0-65535, 0 is off: the force-feedback device, else Moss's PWM motor
int GetRumbleBackend(void); // 0 none, 1 force feedback, 2 PWM

int GetMute(void);
void SetMute(int value); // 0-1

#endif  // __msettings_h__
