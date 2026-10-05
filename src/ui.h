#pragma once
// The screen as the app's code sees it: one 1920x1080 picture of 0xAARRGGBB, drawn with draw.h (5x7
// letters and controller-button icons). ps5/platform.c on the console and tests/sim.c on the PC
// both provide the three calls.
#include <stdint.h>
#include <string.h>

#define WIDTH 1920
#define HEIGHT 1080
#include "draw.h"

#define BG 0x101923
#define PANEL 0x1a2735
#define ACCENT 0x55ddb0
#define WARN 0xffbf69
#define BAD 0xff6b6b
#define DIM 0xaabaca
#define WHITE 0xffffff

extern int32_t ui_user;
uint32_t *begin_frame(void);
void end_frame(void);
uint32_t read_buttons(void);
