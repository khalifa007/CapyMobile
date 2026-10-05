#pragma once
// The game's sound on its way to the console (audio.c): whatever rate the engine produces is brought to
// 48 kHz and played by a thread of its own, so a slow frame never blocks on the sound and a late one
// plays silence instead of a stutter.
#include <stddef.h>
#include <stdint.h>

void audio_open(void);
// 16-bit stereo at rate; safe from the thread that runs the game.
void audio_push(const int16_t *stereo, size_t frames, int rate);
// Drops what is waiting (a pause, the end of a game).
void audio_quiet(void);
// "On", or which step failed with its code.
void audio_status(char *text, size_t size);
