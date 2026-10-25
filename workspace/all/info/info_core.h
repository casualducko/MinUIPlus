// The System Info screen's formatting and parsing, with no graphics (see info.c).
#ifndef INFO_CORE_H
#define INFO_CORE_H
#include <stdint.h>
#include <stddef.h>

void Info_bytes(uint64_t bytes, char* out, size_t size); // "119.1 GB", "512 MB", "3.2 KB", "40 B" (1024 based)
void Info_duration(long seconds, char* out, size_t size); // "40 s", "12 min", "3 h 05 min", "2 d 3 h"
int Info_meminfo(const char* text, uint64_t* total, uint64_t* available); // /proc/meminfo: bytes; 1 if both were found
void Info_temperature(const char* text, char* out, size_t size); // thermal_zone temp (millidegrees): "47.5 C"; "" if not a number
void Info_frequency(const char* text, char* out, size_t size); // cpufreq (kHz): "1.42 GHz", "816 MHz"; "" if not a number

#endif
