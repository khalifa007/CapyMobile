// Checks the sound path of audio.c on the PC: a 1 kHz tone pushed the way the engine pushes sound
// (44.1 kHz, one screen frame's worth every 1/60 s) must come out of the 48 kHz side as a 1 kHz tone
// without jumps. The output thread writes the WAV named by CAPY_MOBILE_WAV; this reads it back.
//   cc -O1 -g -Wall -Wextra -D_GNU_SOURCE -Isrc tests/audio_test.c src/audio.c src/log.c -lpthread -lm -o audio_test
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "audio.h"

int main(void) {
    const char *path = "/tmp/capy-mobile-audio-test.wav";
    setenv("CAPY_MOBILE_WAV", path, 1);
    audio_open();
    usleep(100000);
    static int16_t block[735 * 2];
    double phase = 0;
    for (int frame = 0; frame < 120; ++frame) {
        for (int i = 0; i < 735; ++i) {
            block[i * 2] = block[i * 2 + 1] = (int16_t)(12000 * sin(phase));
            phase += 2 * M_PI * 1000 / 44100;
        }
        audio_push(block, 735, 44100);
        usleep(16666);
    }
    usleep(300000);

    FILE *file = fopen(path, "rb");
    if (!file) { printf("FAIL: no WAV was written\n"); return 1; }
    fseek(file, 44, SEEK_SET);
    static int16_t out[48000 * 4 * 2];
    size_t frames = fread(out, 4, 48000 * 4, file);
    fclose(file);
    // The tone's part: from the first loud sample to the last.
    size_t first = 0, last = 0;
    for (size_t i = 0; i < frames; ++i)
        if (abs(out[i * 2]) > 6000) { if (!first) first = i; last = i; }
    int crossings = 0, worst_jump = 0, silent_runs = 0, run = 0;
    for (size_t i = first + 1; i <= last; ++i) {
        if ((out[i * 2] >= 0) != (out[i * 2 - 2] >= 0)) ++crossings;
        int jump = abs(out[i * 2] - out[i * 2 - 2]);
        if (jump > worst_jump) worst_jump = jump;
        run = out[i * 2] == 0 && out[i * 2 - 2] == 0 ? run + 1 : 0;
        if (run == 48) ++silent_runs;  // A millisecond of nothing in the middle of the tone: a gap
    }
    double seconds = (double)(last - first) / 48000, hertz = seconds > 0 ? crossings / 2.0 / seconds : 0;
    // A 12000-high 1 kHz sine moves at most 12000 * 2 * pi * 1000 / 48000 = 1571 a sample.
    printf("tone: %.2f s of the 2.00 pushed, %.0f Hz, largest step %d (a clean tone: 1571), gaps %d\n", seconds, hertz, worst_jump, silent_runs);
    int ok = seconds > 1.8 && seconds < 2.1 && hertz > 990 && hertz < 1010 && worst_jump < 2400 && !silent_runs;
    printf(ok ? "PASS\n" : "FAIL\n");
    return !ok;
}
