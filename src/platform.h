#pragma once
// The PS5 under the app: its screen, controller, clock and notifications (ps5/platform.c; the
// simulator in tests/sim.c stands in for it on the PC). begin_frame(), end_frame() and
// read_buttons() are declared in ui.h.
#include <stddef.h>
#include <stdint.h>

// libScePad's button bits.
#define PAD_L3 0x2u
#define PAD_R3 0x4u
#define PAD_OPTIONS 0x8u
#define PAD_UP 0x10u
#define PAD_RIGHT 0x20u
#define PAD_DOWN 0x40u
#define PAD_LEFT 0x80u
#define PAD_L2 0x100u
#define PAD_R2 0x200u
#define PAD_L1 0x400u
#define PAD_R1 0x800u
#define PAD_TRIANGLE 0x1000u
#define PAD_CIRCLE 0x2000u
#define PAD_CROSS 0x4000u
#define PAD_SQUARE 0x8000u
#define PAD_ARROWS (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)

// Opens the screen: 0, or a negative step number (the caller has no screen to say it on).
int screen_open(void);
// While held, end_frame() shows nothing, so one screen can be drawn over another (the keyboard).
void frame_hold(int on);
// Opens the first controller of the user who started the app.
void pad_open(void);
// Microseconds since the app started.
uint64_t now_us(void);
void sleep_ms(unsigned ms);
// A system notification, for the moments before the screen is up.
void notify(const char *text);
// The system software as "13.60", or "" when the console does not say.
void firmware_text(char *out, size_t cap);
// How long the last frames took to draw and show, in microseconds (for the log).
unsigned frame_cost_us(void);
// Frames shown since the app started.
unsigned frames_shown(void);
