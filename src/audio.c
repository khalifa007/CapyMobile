// Sound out (audio.h). The output is opened the way the other PS5 emulators open it: the system
// user, the main port, 256 frames, 48 kHz, 16-bit stereo. On Linux
// (the simulator) the blocks go to the WAV file named by CAPY_MOBILE_WAV, or nowhere.
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "log.h"

#define RATE 48000
#define BLOCK 256             // What the console takes at a time: 5.3 ms
#define RING 16384            // Frames; a power of two
#define KEEP (RATE / 8)       // More than this waiting (125 ms) and the oldest is dropped, so sound never lags

static int16_t ring[RING * 2];
static size_t head, count;    // Guarded by lock
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int failed_step, failed_code, ready;
// The resampler's place between two input frames, and the last frame of the block before.
static uint32_t phase;
static int16_t last[2];
static int last_rate;

#ifdef __linux__
#include <unistd.h>
static FILE *wav;
static long wav_frames;
static int out_open(void) {
    const char *path = getenv("CAPY_MOBILE_WAV");
    if (path && (wav = fopen(path, "wb"))) {
        uint8_t header[44] = {0};
        fwrite(header, 1, sizeof(header), wav);
    }
    return 0;
}
static int out_write(const int16_t *block) {
    if (wav) {
        fwrite(block, 4, BLOCK, wav);
        wav_frames += BLOCK;
        // The header is kept right as the file grows: the simulator may be stopped at any moment.
        uint32_t bytes = (uint32_t)wav_frames * 4, riff = bytes + 36, rate = RATE, byte_rate = RATE * 4, fmt = 16;
        uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
        long at = ftell(wav);
        rewind(wav);
        fwrite("RIFF", 1, 4, wav); fwrite(&riff, 4, 1, wav); fwrite("WAVEfmt ", 1, 8, wav); fwrite(&fmt, 4, 1, wav);
        fwrite(&pcm, 2, 1, wav); fwrite(&channels, 2, 1, wav); fwrite(&rate, 4, 1, wav); fwrite(&byte_rate, 4, 1, wav);
        fwrite(&align, 2, 1, wav); fwrite(&bits, 2, 1, wav); fwrite("data", 1, 4, wav); fwrite(&bytes, 4, 1, wav);
        fseek(wav, at, SEEK_SET);
    }
    usleep(BLOCK * 1000000 / RATE);
    return 0;
}
#else
int sceAudioOutInit(void);
int sceAudioOutOpen(int32_t user, int32_t type, int32_t index, uint32_t frames, uint32_t rate, uint32_t format);
int sceAudioOutOutput(int32_t port, const void *samples);
extern int32_t ui_user;
static int port = -1;
static int out_open(void) {
    sceAudioOutInit();  // Not checked: an output that is initialised already answers with an error
    port = sceAudioOutOpen(0xff, 0, 0, BLOCK, RATE, 1);
    if (port < 0 && ui_user >= 0) {
        int first = port;
        port = sceAudioOutOpen(ui_user, 0, 0, BLOCK, RATE, 1);
        if (port < 0) port = first;
    }
    return port < 0 ? port : 0;
}
static int out_write(const int16_t *block) { return sceAudioOutOutput(port, block); }
#endif

static void *player(void *unused) {
    (void)unused;
    int code = out_open();
    if (code) {
        log_line("sound: the output did not open (%08X)", (unsigned)code);
        atomic_store(&failed_code, code);
        atomic_store(&failed_step, 1);
        return NULL;
    }
    atomic_store(&ready, 1);
    // The console reads a block while the next call waits, so two take turns.
    static int16_t block[2][BLOCK * 2] __attribute__((aligned(16)));
    int turn = 0;
    for (;;) {
        int16_t *out = block[turn ^= 1];
        pthread_mutex_lock(&lock);
        size_t take = count < BLOCK ? count : BLOCK;
        for (size_t i = 0; i < take; ++i) {
            size_t at = (head + i) & (RING - 1);
            out[i * 2] = ring[at * 2];
            out[i * 2 + 1] = ring[at * 2 + 1];
        }
        head = (head + take) & (RING - 1);
        count -= take;
        pthread_mutex_unlock(&lock);
        // A short block ends in silence that fades from its last frame, so a gap does not click.
        for (size_t i = take; i < BLOCK; ++i) {
            int fade = take ? (int)(BLOCK - i) : 0;
            out[i * 2] = take ? (int16_t)(out[take * 2 - 2] * fade / BLOCK) : 0;
            out[i * 2 + 1] = take ? (int16_t)(out[take * 2 - 1] * fade / BLOCK) : 0;
        }
        code = out_write(out);
        if (code < 0) {
            log_line("sound: playing failed (%08X)", (unsigned)code);
            atomic_store(&failed_code, code);
            atomic_store(&failed_step, 2);
            atomic_store(&ready, 0);
            return NULL;
        }
    }
}

void audio_open(void) {
    pthread_t thread;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 256 * 1024);  // The console's default is 64 KiB
    int code = pthread_create(&thread, &attributes, player, NULL);
    pthread_attr_destroy(&attributes);
    if (code) { atomic_store(&failed_code, code); atomic_store(&failed_step, 3); return; }
    pthread_detach(thread);
}

static void put(int16_t left, int16_t right) {
    if (count == RING) { head = (head + 1) & (RING - 1); --count; }
    size_t at = (head + count) & (RING - 1);
    ring[at * 2] = left;
    ring[at * 2 + 1] = right;
    ++count;
}

void audio_push(const int16_t *stereo, size_t frames, int rate) {
    if (!atomic_load(&ready) || rate < 4000 || rate > 192000) return;
    pthread_mutex_lock(&lock);
    if (rate != last_rate) { last_rate = rate; phase = 0; last[0] = last[1] = 0; }
    // Straight lines between input frames. phase counts 1/65536ths of an input frame past the
    // frame before index, where "before the first" is the block before's last.
    uint32_t step = (uint32_t)(((uint64_t)rate << 16) / RATE);
    size_t index = 0;
    while (index < frames) {
        const int16_t *a = index ? stereo + (index - 1) * 2 : last, *b = stereo + index * 2;
        int weight = (int)(phase >> 1);  // 0..32767
        put((int16_t)(a[0] + (b[0] - a[0]) * weight / 32768), (int16_t)(a[1] + (b[1] - a[1]) * weight / 32768));
        phase += step;
        index += phase >> 16;
        phase &= 0xffff;
    }
    if (frames) { last[0] = stereo[frames * 2 - 2]; last[1] = stereo[frames * 2 - 1]; }
    if (count > KEEP + BLOCK * 4) { size_t drop = count - KEEP; head = (head + drop) & (RING - 1); count -= drop; }
    pthread_mutex_unlock(&lock);
}

void audio_quiet(void) {
    pthread_mutex_lock(&lock);
    head = count = 0;
    last[0] = last[1] = 0;
    pthread_mutex_unlock(&lock);
}

void audio_status(char *text, size_t size) {
    static const char *const steps[] = {"", "opening the output", "playing", "its thread"};
    int step = atomic_load(&failed_step);
    if (step) snprintf(text, size, "Off: %s failed (%08X)", steps[step], (unsigned)atomic_load(&failed_code));
    else snprintf(text, size, "%s", atomic_load(&ready) ? "On" : "Starting");
}
