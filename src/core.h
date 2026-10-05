#pragma once
// The Java phone-game engine as this app drives it (core.c). The engine is noJMe's libretro core,
// linked into the app: core.c is the small "frontend" such a core expects, and nothing else in the app
// knows about libretro.
#include <stddef.h>
#include <stdint.h>

// A phone's keys, one bit each.
#define KEY_UP 0x1u
#define KEY_DOWN 0x2u
#define KEY_LEFT 0x4u
#define KEY_RIGHT 0x8u
#define KEY_FIRE 0x10u  // The middle key of the pad; most games also take 5 for it
#define KEY_SOFT_LEFT 0x20u
#define KEY_SOFT_RIGHT 0x40u
#define KEY_STAR 0x80u
#define KEY_HASH 0x100u
#define KEY_DIGIT(n) (0x200u << (n))  // 0 to 9

// Where the engine hands its sound: 16-bit stereo at the given rate.
typedef void (*CoreSound)(const int16_t *stereo, size_t frames, int rate);

// Once, before anything else. saves is the folder the games keep their records in.
void core_open(const char *saves, CoreSound sound);
// The phone screen the next game gets: "auto" (what its manifest names, else 240x320) or a size the
// engine knows, like "640x360".
void core_screen(const char *size);
// 0: the game runs about as fast as a phone of its time. 1: as fast as the console goes, a slice of
// each frame. Takes effect at once, in a running game too.
void core_speed(int turbo);
// Starts a .jar: 0, or -1 when the engine would not take it (core_problem() says why).
int core_load(const char *jar);
// Runs the game for one screen frame with these keys held.
void core_run(uint32_t keys);
// The game's screen as it stands: rows of 0x00RRGGBB, width by height. NULL before the first picture.
const uint32_t *core_picture(int *width, int *height);
// 1 once the game has closed itself (its own Exit), or stopped on an error.
int core_ended(void);
const char *core_problem(void);
void core_unload(void);
