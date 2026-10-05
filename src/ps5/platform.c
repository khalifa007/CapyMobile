// The PS5's screen, controller and clock (platform.h). The app draws into one plain 1920x1080
// picture; end_frame() copies it into the console's tiled video buffer. The VideoOut set-up follows blackbearreloaded/ps5-native-app-boilerplate's demo renderer.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "platform.h"
#include "memory.h"

size_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t length, size_t alignment,
                                  int memory_type, int64_t *physical_address);
int sceKernelMapDirectMemory(void **address, size_t length, int protection, int flags, int64_t physical_address,
                             size_t alignment);
int sceKernelSendNotificationRequest(uint32_t device, void *request, size_t size, int blocking);
int sceKernelUsleep(uint32_t microseconds);
uint64_t sceKernelGetProcessTime(void);
int sysctlbyname(const char *name, void *old, size_t *old_size, const void *new, size_t new_size);
int sceSystemServiceHideSplashScreen(void);
int sceVideoOutOpen(int32_t user_id, int32_t bus_type, int32_t index, const void *param);
int sceVideoOutSetFlipRate(int32_t handle, int32_t rate);
int sceVideoOutSubmitFlip(int32_t handle, int32_t buffer_index, uint32_t flip_mode, int64_t flip_argument);
int sceVideoOutWaitVblank(int32_t handle);
typedef struct { void *data, *metadata, *reserved0, *reserved1; } VideoBuffer;
typedef struct { uint8_t reserved[80]; } VideoAttribute;
void sceVideoOutSetBufferAttribute2(VideoAttribute *attribute, uint64_t pixel_format, uint32_t tiling_mode,
                                    uint32_t width, uint32_t height, uint64_t option, uint32_t dcc_control,
                                    uint64_t dcc_clear_color);
int sceVideoOutRegisterBuffers2(int32_t handle, int32_t set_index, int32_t buffer_index_start, VideoBuffer *buffers,
                                int32_t buffer_count, VideoAttribute *attribute, int32_t category, void *option);
int sceUserServiceInitialize(const void *params);
int sceUserServiceGetInitialUser(int32_t *user_id);
int scePadInit(void);
int scePadOpen(int32_t user_id, int32_t type, int32_t index, const void *param);
int scePadReadState(int32_t handle, void *data);

// One video buffer: 16 MiB holds the tiled 1920x1080 picture (135 blocks of 64 KiB).
#define FRAME_BYTES 0x1000000u
#define BLOCK 128
#define BLOCKS_ACROSS ((WIDTH + BLOCK - 1) / BLOCK)
#define BLOCKS_DOWN ((HEIGHT + BLOCK - 1) / BLOCK)
#define PIXEL_FORMAT_RGBA8_SRGB 0x8000000022000000ull

int32_t ui_user = -1;
static int video = -1, pad = -1, held, back;
static uint8_t *buffers[2];
static uint32_t *frame;  // What the screens draw into: 0xAARRGGBB, row after row
static int64_t flips;
static uint16_t x_part[BLOCK], y_part[BLOCK];
static unsigned cost_us, frames;
static uint64_t shown_at;

uint64_t now_us(void) { return sceKernelGetProcessTime(); }
void sleep_ms(unsigned ms) { sceKernelUsleep(ms * 1000u); }
unsigned frame_cost_us(void) { return cost_us; }
unsigned frames_shown(void) { return frames; }

void notify(const char *text) {
    static struct { uint8_t reserved[45]; char message[3075]; } request;
    snprintf(request.message, sizeof(request.message), "%s", text);
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

void firmware_text(char *out, size_t cap) {
    uint32_t version = 0;
    size_t size = sizeof(version);
    if (cap) out[0] = 0;
    // Hex digits that read as the number: 0x13600007 is 13.60.
    if (!sysctlbyname("kern.sdk_version", &version, &size, NULL, 0) && version)
        snprintf(out, cap, "%x.%02x", (unsigned)(version >> 24), (unsigned)(version >> 16 & 0xff));
}

// Where a pixel sits inside its 128x128 block of the tiled buffer: the two parts are combined with XOR.
static void tile_tables(void) {
    for (unsigned i = 0; i < BLOCK; ++i) {
        x_part[i] = (uint16_t)(((i << 2) & 0xc) ^ ((i << 5) & 0x380) ^ ((i << 4) & 0x400) ^ ((i << 6) & 0x800) ^ ((i << 9) & 0xa000));
        y_part[i] = (uint16_t)(((i << 4) & 0x70) ^ ((i << 5) & 0xf00) ^ ((i << 9) & 0x1000) ^ ((i << 8) & 0x4000));
    }
}

int screen_open(void) {
    sceSystemServiceHideSplashScreen();
    video = sceVideoOutOpen(0xff, 0, 0, NULL);
    if (video < 0) return -1;
    int64_t physical = 0;
    void *mapped = NULL;
    if (sceKernelGetDirectMemorySize() < 2 * (size_t)FRAME_BYTES) return -2;
    if (sceKernelAllocateDirectMemory(0, (int64_t)sceKernelGetDirectMemorySize(), 2 * (size_t)FRAME_BYTES, 0x200000, 3, &physical) < 0) return -3;
    if (sceKernelMapDirectMemory(&mapped, 2 * (size_t)FRAME_BYTES, 0x33, 0, physical, 0x200000) < 0) return -4;
    buffers[0] = mapped;
    buffers[1] = (uint8_t *)mapped + FRAME_BYTES;
    memset(mapped, 0, 2 * (size_t)FRAME_BYTES);
    frame = big_alloc((size_t)WIDTH * HEIGHT * sizeof(uint32_t));
    if (!frame) return -5;
    tile_tables();
    VideoBuffer registered[2] = {{buffers[0], NULL, NULL, NULL}, {buffers[1], NULL, NULL, NULL}};
    VideoAttribute attribute;
    memset(&attribute, 0, sizeof(attribute));
    sceVideoOutSetFlipRate(video, 0);
    sceVideoOutSetBufferAttribute2(&attribute, PIXEL_FORMAT_RGBA8_SRGB, 0, WIDTH, HEIGHT, 0, 0, 0);
    if (sceVideoOutRegisterBuffers2(video, 0, 0, registered, 2, &attribute, 0, NULL) < 0) return -6;
    return 0;
}

void frame_hold(int on) { held = on; }

uint32_t *begin_frame(void) { return frame; }

// Copies the drawn picture into the buffer that is not on screen and shows it at the next refresh.
// Each 64 KiB block is put together in ordinary memory first: the video buffer is write-combined, and
// writing its pixels in tile order straight away would be slow.
void end_frame(void) {
    static uint8_t block[BLOCK * BLOCK * 4];
    if (held || video < 0) return;
    uint64_t start = now_us();
    uint8_t *out = buffers[back];
    for (int by = 0; by < BLOCKS_DOWN; ++by) {
        int rows = HEIGHT - by * BLOCK < BLOCK ? HEIGHT - by * BLOCK : BLOCK;
        for (int bx = 0; bx < BLOCKS_ACROSS; ++bx) {
            int columns = WIDTH - bx * BLOCK < BLOCK ? WIDTH - bx * BLOCK : BLOCK;
            for (int y = 0; y < rows; ++y) {
                const uint32_t *in = frame + (size_t)(by * BLOCK + y) * WIDTH + bx * BLOCK;
                unsigned down = y_part[y];
                for (int x = 0; x < columns; ++x) {
                    // The screens use 0xAARRGGBB; the buffer wants red in the low byte.
                    uint32_t p = in[x], rgba = 0xff000000u | (p & 0xff00u) | (p >> 16 & 0xffu) | (p & 0xffu) << 16;
                    memcpy(block + (x_part[x] ^ down), &rgba, sizeof(rgba));
                }
            }
            memcpy(out + ((size_t)(by * BLOCKS_ACROSS + bx) << 16), block, sizeof(block));
        }
    }
    __asm__ volatile("sfence" ::: "memory");
    sceVideoOutSubmitFlip(video, back, 1, ++flips);
    cost_us = (unsigned)(now_us() - start);
    // The other buffer is free once this one is on screen.
    sceVideoOutWaitVblank(video);
    // Should that wait ever return at once, the loop must still not spin: it would take a processor
    // core from the install's threads. A frame is given at least 12 ms.
    uint64_t now = now_us();
    if (shown_at && now - shown_at < 12000) sceKernelUsleep((uint32_t)(12000 - (now - shown_at)));
    shown_at = now_us();
    ++frames;
    back ^= 1;
}

void pad_open(void) {
    sceUserServiceInitialize(NULL);
    if (sceUserServiceGetInitialUser(&ui_user) < 0 || ui_user < 0) return;
    scePadInit();
    pad = scePadOpen(ui_user, 0, 0, NULL);
}

// The left stick moves like the D-pad: past the dead zone its main direction counts as that button.
#define STICK_DEAD 64
static uint32_t stick_buttons(int x, int y) {
    int dx = x - 128, dy = y - 128, ax = abs(dx), ay = abs(dy);
    if (ax < STICK_DEAD && ay < STICK_DEAD) return 0;
    if (ax >= ay) return dx < 0 ? PAD_LEFT : PAD_RIGHT;
    return dy < 0 ? PAD_UP : PAD_DOWN;
}

uint32_t read_buttons(void) {
    // libScePad's state: the buttons, then the sticks and triggers; the rest is not used here.
    struct { uint32_t buttons; uint8_t lx, ly, rx, ry, l2, r2, pad0, pad1; uint8_t rest[256]; } state;
    memset(&state, 0, sizeof(state));
    state.lx = state.ly = 128;
    if (pad < 0 || scePadReadState(pad, &state) != 0) return 0;
    return state.buttons | stick_buttons(state.lx, state.ly);
}
