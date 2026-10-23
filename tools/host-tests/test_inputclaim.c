// minarch's MENU+button handling: a held MENU+button mapping claims its button
#include <stdio.h>
#include <string.h>
enum { BTN_ID_A, BTN_ID_B, BTN_ID_X, BTN_ID_MENU, BTN_ID_DPAD_UP, BTN_ID_UP };
#define BTN_NONE 0
#define BTN_A (1<<BTN_ID_A)
#define BTN_B (1<<BTN_ID_B)
#define BTN_X (1<<BTN_ID_X)
#define BTN_MENU (1<<BTN_ID_MENU)
#define BTN_DPAD_UP (1<<BTN_ID_DPAD_UP)
#define BTN_UP (1<<BTN_ID_UP)
#define BTN_DPAD_DOWN 0x1000
#define BTN_DPAD_LEFT 0x2000
#define BTN_DPAD_RIGHT 0x4000
#define BTN_DOWN 0x8000
#define BTN_LEFT 0x10000
#define BTN_RIGHT 0x20000
#define SHORTCUT_COUNT 2
typedef struct { char* name; int retro; int local; int mod; int ignore; } ButtonMapping;
static struct { ButtonMapping* controls; ButtonMapping* shortcuts; } config;
static int pressed_mask;
static int PAD_isPressed(int btn) { return (pressed_mask & btn) != 0; }
static int gamepad_type = 1;
static int buttons;
static int ignore_menu;
static void poll(void) {
	// --- the real minarch code, extracted verbatim ---
#include "gen/inputclaim_claim.inc"
#include "gen/inputclaim_controls.inc"
}
static int fails;
static void expect(const char* what, int got, int want) {
	printf("%s  %-64s retro bits=0x%x (want 0x%x)\n", got==want?"ok  ":"FAIL", what, got, want);
	if (got!=want) fails++;
}
int main(void) {
	ButtonMapping controls[] = {
		{"A",      0, BTN_ID_A, 0, 0},   // plain A -> retro bit 0
		{"X",      1, BTN_ID_X, 0, 0},   // plain X -> retro bit 1
		{"MENU+A", 2, BTN_ID_A, 1, 0},   // MENU+A  -> retro bit 2
		{NULL}
	};
	ButtonMapping shortcuts[] = { {"Save State", 0, BTN_ID_X, 1, 0}, {"Load State", 0, -1, 0, 0} };
	config.controls = controls; config.shortcuts = shortcuts;

	pressed_mask = BTN_A;                poll(); expect("A alone: plain A reaches the game", buttons, 1<<0);
	pressed_mask = BTN_X;                poll(); expect("X alone: plain X reaches the game", buttons, 1<<1);
	pressed_mask = BTN_MENU|BTN_X;       poll(); expect("MENU+X (a shortcut): plain X must not reach the game", buttons, 0);
	pressed_mask = BTN_MENU|BTN_A;       poll(); expect("MENU+A (a control): only that control, not plain A", buttons, 1<<2);
	pressed_mask = BTN_MENU|BTN_A|BTN_X; poll(); expect("MENU+A+X: A claimed by a control, X by a shortcut", buttons, 1<<2);
	pressed_mask = BTN_MENU;             poll(); expect("MENU alone: nothing", buttons, 0);

	ButtonMapping controls2[] = { {"B", 3, BTN_ID_B, 0, 0}, {NULL} };
	ButtonMapping shortcuts2[] = { {"s", 0, -1, 0, 0}, {"t", 0, -1, 0, 0} };
	config.controls = controls2; config.shortcuts = shortcuts2;
	pressed_mask = BTN_MENU|BTN_B;       poll(); expect("MENU+B with no MENU+B mapping: plain B still reaches the game", buttons, 1<<3);
	pressed_mask = BTN_B;                poll(); expect("default config (no MENU+ mappings): unchanged", buttons, 1<<3);
	return fails!=0;
}
