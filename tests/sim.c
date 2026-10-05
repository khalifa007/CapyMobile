// Runs Capy Mobile's own main loop on Linux or WSL with a scripted controller and saves chosen frames as
// PNGs: the console cannot be watched from a PC, so this is how the screens are checked.
//   sim <out folder> <script>
// The script is words: a button (U D L R X O SQ TRI L1 R1 L2 R2 L3 R3 OPT) is pressed for a few
// frames, "hold:<button>:<frames>" holds one, "w<frames>" waits, "s:<name>" saves <name>.png.
// The games folder is $CAPY_MOBILE_HOME/games. It stands in for ps5/platform.c and ps5/memory.c only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ui.h"
#include "platform.h"
#include "memory.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image_write.h"

int app_main(void);

typedef struct { uint32_t buttons; char snap[48]; } Frame;
static Frame *script;
static int script_len, script_cap, frame_no;
static const char *out_folder;
static uint32_t frame[WIDTH * HEIGHT];
int32_t ui_user = 1;

static void add(uint32_t buttons, const char *snap) {
    if (script_len == script_cap) script = realloc(script, sizeof(Frame) * (size_t)(script_cap = script_cap ? script_cap * 2 : 1024));
    script[script_len].buttons = buttons;
    snprintf(script[script_len++].snap, sizeof(script[0].snap), "%s", snap ? snap : "");
}

static uint32_t button_named(const char *word) {
    static const struct { const char *name; uint32_t bit; } names[] = {
        {"U", PAD_UP}, {"D", PAD_DOWN}, {"L", PAD_LEFT}, {"R", PAD_RIGHT}, {"X", PAD_CROSS}, {"O", PAD_CIRCLE},
        {"SQ", PAD_SQUARE}, {"TRI", PAD_TRIANGLE}, {"L1", PAD_L1}, {"R1", PAD_R1}, {"L2", PAD_L2}, {"R2", PAD_R2},
        {"L3", PAD_L3}, {"R3", PAD_R3}, {"OPT", PAD_OPTIONS},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(word, names[i].name)) return names[i].bit;
    fprintf(stderr, "sim: unknown word in the script: %s\n", word);
    exit(2);
}

static void read_script(char *text) {
    for (char *word = strtok(text, " "); word; word = strtok(NULL, " ")) {
        if (word[0] == 'w' && word[1] >= '0' && word[1] <= '9') {
            for (int n = atoi(word + 1); n > 0; --n) add(0, NULL);
        } else if (!strncmp(word, "s:", 2)) {
            add(0, word + 2);
        } else if (!strncmp(word, "hold:", 5)) {
            char *count = strchr(word + 5, ':');
            if (!count) { fprintf(stderr, "sim: hold:<button>:<frames>\n"); exit(2); }
            *count = 0;
            uint32_t bit = button_named(word + 5);
            for (int n = atoi(count + 1); n > 0; --n) add(bit, NULL);
            add(0, NULL);
        } else {
            uint32_t bit = button_named(word);
            for (int i = 0; i < 4; ++i) add(bit, NULL);
            for (int i = 0; i < 6; ++i) add(0, NULL);
        }
    }
}

int screen_open(void) { return 0; }
void frame_hold(int on) { (void)on; }
void pad_open(void) {}
void notify(const char *text) { fprintf(stderr, "notify: %s\n", text); }
void firmware_text(char *out, size_t cap) { snprintf(out, cap, "sim"); }
unsigned frame_cost_us(void) { return 0; }
unsigned frames_shown(void) { return (unsigned)frame_no; }
void sleep_ms(unsigned ms) { usleep(ms * 1000); }
uint64_t now_us(void) {
    static struct timespec start;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!start.tv_sec) start = now;
    return (uint64_t)(now.tv_sec - start.tv_sec) * 1000000u + (uint64_t)now.tv_nsec / 1000 - (uint64_t)start.tv_nsec / 1000;
}
void *big_alloc(size_t size) { return calloc(1, size); }
void big_free(void *block, size_t size) { (void)size; free(block); }
long big_free_megabytes(void) { return -1; }

uint32_t *begin_frame(void) { return frame; }
uint32_t read_buttons(void) { return frame_no < script_len ? script[frame_no].buttons : 0; }

void end_frame(void) {
    static uint64_t shown;
    if (frame_no < script_len && script[frame_no].snap[0]) {
        static uint8_t rgb[WIDTH * HEIGHT * 3];
        for (int i = 0; i < WIDTH * HEIGHT; ++i) {
            rgb[i * 3] = (uint8_t)(frame[i] >> 16);
            rgb[i * 3 + 1] = (uint8_t)(frame[i] >> 8);
            rgb[i * 3 + 2] = (uint8_t)frame[i];
        }
        char path[400];
        snprintf(path, sizeof(path), "%s/%s.png", out_folder, script[frame_no].snap);
        stbi_write_png(path, WIDTH, HEIGHT, 3, rgb, WIDTH * 3);
        fprintf(stderr, "sim: saved %s at frame %d\n", path, frame_no);
    }
    if (++frame_no >= script_len) { fprintf(stderr, "sim: the script is over after %d frames\n", frame_no); exit(0); }
    // The engine keeps time by the real clock, so the frames take their real 1/60 s.
    uint64_t now = now_us();
    if (now - shown < 16666) usleep((useconds_t)(16666 - (now - shown)));
    shown = now_us();
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: sim <out folder> <script>\n"); return 2; }
    out_folder = argv[1];
    read_script(argv[2]);
    return app_main();
}
