// The Switcher tool's logic with no graphics (see switcher.c): the recent games list and how a save
// state is described. recent.txt has one game per line, "<path below the card>[<tab><alias>]", newest first.
#ifndef SWITCHER_CORE_H
#define SWITCHER_CORE_H
#include <stddef.h>

#define SW_PATH_MAX 512

// Paths of the games in text (which is edited), newest first, at most max. A blank line or one with no path is skipped.
int SW_parseRecents(char* text, char paths[][SW_PATH_MAX], int max);
// text without the line for path, in out; returns its length (the text unchanged if the path isn't there)
int SW_dropRecent(const char* text, const char* path, char* out, size_t cap);
// the slot a .minui/<EMU>/<game>.txt names: 0 to 9, or -1 for anything else (an empty file, text, a number out of range)
int SW_parseSlot(const char* text);
// Which state a game resumes from: -1 for none; otherwise the slot you saved to last (manual_slot, 0 to 8, -1 if
// none) or 9, the auto-save a quit or a sleep makes, whichever picture is newer. A picture time of 0 means the
// slot has no picture (a manual slot is still a state; the auto-save only counts with its picture).
int SW_pickSlot(int manual_slot, long manual_at, long auto_at);
// "just now", "5 minutes ago", "3 hours ago", "2 days ago", "4 months ago", "2 years ago"
void SW_ago(long seconds, char* out, size_t cap);
// the file name part of a path
const char* SW_baseName(const char* path);

#endif
