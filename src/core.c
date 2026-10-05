// The frontend half of libretro for one core, noJMe (core.h). The core asks for its settings, its save
// folder and the keys through callbacks and hands back pictures and sound the same way.
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "core.h"
#include "libretro.h"
#include "log.h"

#define MAX_SIDE 800  // The core's largest screen, either way (its retro_get_system_av_info)

static uint32_t picture[MAX_SIDE * MAX_SIDE];
static int width, height, have_picture, loaded, ended, format = RETRO_PIXEL_FORMAT_RGB565;
static uint32_t held;
static char save_folder[256], problem[160];
static CoreSound sound_sink;
static int sound_rate = 44100;

// The core's settings. Anything not listed keeps the core's own default.
static const struct { const char *key, *value; } settings[] = {
    {"j2me_fps", "60"},              // One core frame per screen frame
    {"j2me_audio_rate", "44100"},
    {"j2me_pixel_format", "RGB888"},
    {"j2me_touch_input", "off"},     // No stylus games yet: the stick stays a D-pad
};

// The engine parks a game whenever retro_run() has not been called for 0.3 s, which is what makes the
// pause menu pause it. That watch is armed at the end of each load and never disarmed, so a second
// game's start-up, which runs before its first frame, would sit parked until a 20 s safety lifts it.
// Disarming it around a load and an unload is the engine's own switch for it (its threads.c).
void jvm_frontend_pause_enable(int on);

static char screen_size[16] = "auto";
void core_screen(const char *size) { snprintf(screen_size, sizeof(screen_size), "%s", size); }
static int turbo, settings_changed;
void core_speed(int on) { settings_changed |= turbo != !!on; turbo = !!on; }

static void core_log(enum retro_log_level level, const char *format_text, ...) {
    if (level < RETRO_LOG_WARN) return;
    char line[400];
    va_list args;
    va_start(args, format_text);
    vsnprintf(line, sizeof(line), format_text, args);
    va_end(args);
    size_t length = strlen(line);
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = 0;
    // Whatever stops a game for good is marked so (the engine's CORE_FATAL); the rest are notes.
    const char *fatal = strstr(line, "[FATAL] ");
    if (fatal) snprintf(problem, sizeof(problem), "%s", fatal + 8);
    log_line("engine: %s", line);
}

static bool environment(unsigned command, void *data) {
    switch (command) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *variable = data;
        variable->value = NULL;
        for (size_t i = 0; variable->key && i < sizeof(settings) / sizeof(settings[0]); ++i)
            if (!strcmp(variable->key, settings[i].key)) variable->value = settings[i].value;
        if (variable->key && !strcmp(variable->key, "j2me_resolution")) variable->value = screen_size;
        if (variable->key && !strcmp(variable->key, "j2me_vm_speed")) variable->value = turbo ? "turbo" : "fast";
        return variable->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = settings_changed != 0;
        settings_changed = 0;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        int wanted = (int)*(const enum retro_pixel_format *)data;
        if (wanted != RETRO_PIXEL_FORMAT_XRGB8888 && wanted != RETRO_PIXEL_FORMAT_RGB565) return false;
        format = wanted;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = save_folder;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        const struct retro_system_av_info *info = data;
        if (info->timing.sample_rate >= 8000) sound_rate = (int)info->timing.sample_rate;
        return true;
    }
    case RETRO_ENVIRONMENT_SHUTDOWN:
        ended = 1;
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 0;  // The plain key/value settings above
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
    default:
        // No rumble, and no turned screen: the core then turns the picture itself.
        return false;
    }
}

static void video(const void *data, unsigned w, unsigned h, size_t pitch) {
    if (!data || !w || !h || w > MAX_SIDE || h > MAX_SIDE) return;  // No data: the last picture stands
    width = (int)w;
    height = (int)h;
    for (unsigned y = 0; y < h; ++y) {
        const uint8_t *row = (const uint8_t *)data + y * pitch;
        uint32_t *out = picture + (size_t)y * w;
        if (format == RETRO_PIXEL_FORMAT_XRGB8888) {
            memcpy(out, row, (size_t)w * 4);
        } else {
            for (unsigned x = 0; x < w; ++x) {
                uint16_t p;
                memcpy(&p, row + x * 2, 2);
                uint32_t r = p >> 11, g = p >> 5 & 0x3f, b = p & 0x1f;
                out[x] = (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
            }
        }
    }
    have_picture = 1;
}

static size_t audio_batch(const int16_t *data, size_t frames) {
    if (sound_sink && data && frames) sound_sink(data, frames, sound_rate);
    return frames;
}
static void audio_sample(int16_t left, int16_t right) {
    int16_t pair[2] = {left, right};
    audio_batch(pair, 1);
}

static void input_poll(void) {}

// The core reads a phone through a game pad plus a few keyboard keys; this is its own mapping read
// backwards (get_key_states() and kb_key_map in its libretro.c).
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)index;
    if (port) return 0;
    uint32_t key = 0;
    if (device == RETRO_DEVICE_JOYPAD) {
        switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_UP: key = KEY_UP; break;
        case RETRO_DEVICE_ID_JOYPAD_DOWN: key = KEY_DOWN; break;
        case RETRO_DEVICE_ID_JOYPAD_LEFT: key = KEY_LEFT; break;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT: key = KEY_RIGHT; break;
        case RETRO_DEVICE_ID_JOYPAD_A: key = KEY_FIRE; break;
        case RETRO_DEVICE_ID_JOYPAD_SELECT: key = KEY_SOFT_LEFT; break;
        case RETRO_DEVICE_ID_JOYPAD_START: key = KEY_SOFT_RIGHT; break;
        case RETRO_DEVICE_ID_JOYPAD_Y: key = KEY_DIGIT(1); break;
        case RETRO_DEVICE_ID_JOYPAD_L: key = KEY_DIGIT(2); break;
        case RETRO_DEVICE_ID_JOYPAD_R: key = KEY_DIGIT(3); break;
        case RETRO_DEVICE_ID_JOYPAD_L2: key = KEY_DIGIT(4); break;
        case RETRO_DEVICE_ID_JOYPAD_R2: key = KEY_DIGIT(5); break;
        default: break;
        }
    } else if (device == RETRO_DEVICE_KEYBOARD) {
        switch (id) {
        case RETROK_0: key = KEY_DIGIT(0); break;
        case RETROK_6: key = KEY_DIGIT(6); break;
        case RETROK_7: key = KEY_DIGIT(7); break;
        case RETROK_8: key = KEY_DIGIT(8); break;
        case RETROK_9: key = KEY_DIGIT(9); break;
        case RETROK_ASTERISK: key = KEY_STAR; break;
        case RETROK_HASH: key = KEY_HASH; break;
        default: break;
        }
    }
    return (held & key) != 0;
}

void core_open(const char *saves, CoreSound sound) {
    snprintf(save_folder, sizeof(save_folder), "%s", saves);
    sound_sink = sound;
    retro_set_environment(environment);
    retro_set_video_refresh(video);
    retro_set_audio_sample(audio_sample);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
}

int core_load(const char *jar) {
    if (loaded) core_unload();
    problem[0] = 0;
    ended = have_picture = 0;
    held = 0;
    struct retro_game_info game;
    memset(&game, 0, sizeof(game));
    game.path = jar;
    jvm_frontend_pause_enable(0);
    // One retro_init() and retro_deinit() per game: the Java machine of a game is only taken down by
    // the second (retro_unload_game() just writes the records), and a game loaded on top of a live
    // one leaves the old game running underneath.
    retro_init();
    if (!retro_load_game(&game)) {
        if (!problem[0]) snprintf(problem, sizeof(problem), "The game did not start");
        retro_deinit();
        return -1;
    }
    struct retro_system_av_info info;
    memset(&info, 0, sizeof(info));
    retro_get_system_av_info(&info);
    if (info.timing.sample_rate >= 8000) sound_rate = (int)info.timing.sample_rate;
    log_line("engine: %s started, %ux%u, sound at %d Hz", jar, info.geometry.base_width, info.geometry.base_height, sound_rate);
    loaded = 1;
    return 0;
}

void core_run(uint32_t keys) {
    if (!loaded || ended) return;
    held = keys;
    retro_run();
}

const uint32_t *core_picture(int *w, int *h) {
    if (!have_picture) return NULL;
    *w = width;
    *h = height;
    return picture;
}

int core_ended(void) { return ended; }
const char *core_problem(void) { return problem; }

void core_unload(void) {
    if (!loaded) return;
    jvm_frontend_pause_enable(0);
    retro_unload_game();  // Writes the game's records to the save folder
    retro_deinit();
    loaded = 0;
    have_picture = 0;
}
