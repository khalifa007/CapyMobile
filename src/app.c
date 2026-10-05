// Capy Mobile: Java phone games (J2ME .jar files) on the PS5. This file is the app itself: the list of
// games found in its games folder, the game on screen with the controller standing in for the phone's
// keys, and the pause menu. The engine is behind core.h, the sound behind audio.h, and the console
// behind platform.h; the simulator (tests/sim.c) replaces only the last.
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "app_version.h"
#include "audio.h"
#include "core.h"
#include "engine_env.h"
#include "jar_reader.h"
#include "log.h"
#include "memory.h"
#include "platform.h"
#include "stb_image.h"
#include "ui.h"

#define MAX_GAMES 512
#define COLUMNS 6
#define ROWS 2
#define TILE 256
#define TILE_STEP_X 288
#define TILE_STEP_Y 340
#define GRID_X 96
#define GRID_Y 230

typedef struct {
    char file[160];   // Its name in the games folder
    char name[64];    // From the manifest, else the file name
    char maker[48];
    long bytes;
    uint32_t *icon;   // 0xAARRGGBB, or NULL
    int icon_w, icon_h;
} Game;

static Game games[MAX_GAMES];
static int game_count, chosen, top_row;
static char games_folder[200], saves_folder[200], message[200];
static uint32_t *px;

// ---------- folders ----------

static int writable(const char *folder) {
    char path[240];
    mkdir(folder, 0777);
    snprintf(path, sizeof(path), "%s/.probe", folder);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return 0;
    close(fd);
    unlink(path);
    return 1;
}

// Calls each(name) for every entry of folder; -1 when it cannot be listed.
static int list_folder(const char *folder, void (*each)(const char *name)) {
#ifdef __linux__
    DIR *dir = opendir(folder);
    if (!dir) return -1;
    for (struct dirent *entry; (entry = readdir(dir));) each(entry->d_name);
    closedir(dir);
    return 0;
#else
    // opendir() is refused inside a PS5 title (the other emulators found the same): the entries are
    // read with the system call under it.
    int getdents(int fd, char *buffer, int size);
    // The read must be at least one block of the folder's drive, or it is refused: /data's are
    // 64 KiB (a 16 KiB read listed nothing on the console).
    int fd = open(folder, O_RDONLY | O_DIRECTORY), got = -1, total = 0;
    static char buffer[256 * 1024];
    while (fd >= 0 && (got = getdents(fd, buffer, (int)sizeof(buffer))) > 0) {
        for (int at = 0; at < got;) {
            struct dirent *entry = (struct dirent *)(buffer + at);
            if (entry->d_reclen == 0) break;
            if (entry->d_fileno) each(entry->d_name);
            at += entry->d_reclen;
            ++total;
        }
    }
    if (fd < 0 || got < 0) log_line("games: listing %s failed at %s (error %d)", folder, fd < 0 ? "open" : "getdents", errno);
    if (fd >= 0) close(fd);
    if (total || got == 0) return 0;
    // That call refused too: the library's way, in case this console allows it after all.
    DIR *dir = opendir(folder);
    if (!dir) return -1;
    for (struct dirent *entry; (entry = readdir(dir));) each(entry->d_name);
    closedir(dir);
    return 0;
#endif
}

// ---------- the games ----------

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    uint8_t *data = length > 0 && length < 256l * 1024 * 1024 ? malloc((size_t)length) : NULL;
    if (data && fread(data, 1, (size_t)length, file) != (size_t)length) { free(data); data = NULL; }
    fclose(file);
    if (data) *size = (size_t)length;
    return data;
}

// The value of "Key: value" in an unfolded manifest, trimmed; "" when it is not there.
static void manifest_value(const char *manifest, const char *key, char *out, size_t cap) {
    out[0] = 0;
    size_t length = strlen(key);
    for (const char *line = manifest; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
        if (strncasecmp(line, key, length) || line[length] != ':') continue;
        const char *value = line + length + 1;
        while (*value == ' ' || *value == '\t') ++value;
        size_t n = strcspn(value, "\r\n");
        while (n && (value[n - 1] == ' ' || value[n - 1] == '\t')) --n;
        if (n >= cap) n = cap - 1;
        memcpy(out, value, n);
        out[n] = 0;
        return;
    }
}

// The font has printable ASCII only. A name written in another alphabet would show as blanks, so it
// counts as no name and the file's is used.
static int readable(const char *text) {
    int letters = 0;
    for (; *text; ++text) {
        if ((unsigned char)*text >= 0x80) return 0;
        if (*text > ' ') ++letters;
    }
    return letters > 0;
}

static void read_game(Game *game, const char *path) {
    size_t size = 0, part = 0;
    uint8_t *jar = read_file(path, &size);
    if (!jar) return;
    game->bytes = (long)size;
    uint8_t *raw = jar_read_file(jar, size, "META-INF/MANIFEST.MF", &part);
    char *manifest = raw ? jar_manifest_unfold(raw, part) : NULL;
    free(raw);
    char icon[128] = "", first[256] = "", name[64] = "";
    if (manifest) {
        manifest_value(manifest, "MIDlet-Name", name, sizeof(name));
        manifest_value(manifest, "MIDlet-Vendor", game->maker, sizeof(game->maker));
        manifest_value(manifest, "MIDlet-Icon", icon, sizeof(icon));
        // "MIDlet-1: name, icon, class" names the icon when MIDlet-Icon does not.
        manifest_value(manifest, "MIDlet-1", first, sizeof(first));
        char *comma = strchr(first, ',');
        if (comma && !icon[0]) {
            char *start = comma + 1, *end = strchr(start, ',');
            if (end) *end = 0;
            while (*start == ' ') ++start;
            size_t n = strlen(start);
            while (n && start[n - 1] == ' ') start[--n] = 0;
            snprintf(icon, sizeof(icon), "%s", start);
        }
        free(manifest);
    }
    if (readable(name)) snprintf(game->name, sizeof(game->name), "%s", name);
    if (!readable(game->maker)) game->maker[0] = 0;
    const char *entry = icon[0] == '/' ? icon + 1 : icon;
    uint8_t *png = entry[0] ? jar_read_file(jar, size, entry, &part) : NULL;
    if (png) {
        int w = 0, h = 0, channels = 0;
        uint8_t *rgba = part < 4u * 1024 * 1024 ? stbi_load_from_memory(png, (int)part, &w, &h, &channels, 4) : NULL;
        if (rgba && w > 0 && h > 0 && w <= 256 && h <= 256 && (game->icon = malloc((size_t)w * h * 4))) {
            for (int i = 0; i < w * h; ++i)
                game->icon[i] = (uint32_t)rgba[i * 4 + 3] << 24 | (uint32_t)rgba[i * 4] << 16 | (uint32_t)rgba[i * 4 + 1] << 8 | rgba[i * 4 + 2];
            game->icon_w = w;
            game->icon_h = h;
        }
        stbi_image_free(rgba);
        free(png);
    }
    free(jar);
}

static void found(const char *name) {
    size_t length = strlen(name);
    if (game_count == MAX_GAMES || length < 5 || length >= sizeof(games[0].file) || strcasecmp(name + length - 4, ".jar")) return;
    Game *game = &games[game_count];
    memset(game, 0, sizeof(*game));
    snprintf(game->file, sizeof(game->file), "%s", name);
    snprintf(game->name, sizeof(game->name), "%.*s", (int)(length - 4 < sizeof(game->name) - 1 ? length - 4 : sizeof(game->name) - 1), name);
    for (char *c = game->name; *c; ++c)
        if (*c == '_' || (unsigned char)*c >= 0x80) *c = ' ';
    char path[400];
    snprintf(path, sizeof(path), "%s/%s", games_folder, name);
    read_game(game, path);
    if (game->bytes > 0) ++game_count;
}

static int by_name(const void *a, const void *b) { return strcasecmp(((const Game *)a)->name, ((const Game *)b)->name); }

static void scan_games(void) {
    char keep[160] = "";
    if (chosen < game_count) snprintf(keep, sizeof(keep), "%s", games[chosen].file);
    for (int i = 0; i < game_count; ++i) free(games[i].icon);
    game_count = 0;
    uint64_t start = now_us();
    int rc = list_folder(games_folder, found);
    qsort(games, (size_t)game_count, sizeof(Game), by_name);
    chosen = 0;
    for (int i = 0; i < game_count; ++i)
        if (!strcmp(games[i].file, keep)) chosen = i;
    log_line("games: %d in %s%s, read in %u ms", game_count, games_folder, rc ? " (the folder could not be listed)" : "",
             (unsigned)((now_us() - start) / 1000));
}

// ---------- drawing ----------

static void fill(int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WIDTH) w = WIDTH - x;
    if (y + h > HEIGHT) h = HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    color |= 0xff000000u;
    for (int row = y; row < y + h; ++row) {
        uint32_t *out = px + (size_t)row * WIDTH + x;
        for (int col = 0; col < w; ++col) out[col] = color;
    }
}

static void frame_around(int x, int y, int w, int h, int thick, uint32_t color) {
    fill(x - thick, y - thick, w + 2 * thick, thick, color);
    fill(x - thick, y + h, w + 2 * thick, thick, color);
    fill(x - thick, y, thick, h, color);
    fill(x + w, y, thick, h, color);
}

// A picture enlarged to w by h with its pixels kept sharp. Pixels that are mostly see-through are left out.
static void enlarge(const uint32_t *source, int sw, int sh, int x, int y, int w, int h, int with_alpha) {
    for (int row = 0; row < h; ++row) {
        int dy = y + row;
        if (dy < 0 || dy >= HEIGHT) continue;
        const uint32_t *in = source + (size_t)(row * sh / h) * sw;
        uint32_t *out = px + (size_t)dy * WIDTH;
        // Rows that come from the same source row are copies of the one above.
        if (!with_alpha && row && row * sh / h == (row - 1) * sh / h && dy > 0 && x >= 0 && x + w <= WIDTH) {
            memcpy(out + x, out - WIDTH + x, (size_t)w * 4);
            continue;
        }
        for (int col = 0; col < w; ++col) {
            int dx = x + col;
            if (dx < 0 || dx >= WIDTH) continue;
            uint32_t p = in[col * sw / w];
            if (!with_alpha || p >> 24 >= 0x80) out[dx] = 0xff000000u | p;
        }
    }
}

static void centred(int cx, int y, int scale, const char *s, uint32_t color) {
    ui_text(px, cx - ui_width(s, scale) / 2, y, scale, s, color);
}

static void cut(char *out, size_t cap, const char *s, size_t most) {
    size_t n = strlen(s);
    if (most >= cap) most = cap - 1;
    if (n <= most) { memcpy(out, s, n + 1); return; }
    memcpy(out, s, most);
    if (most >= 2) out[most - 1] = out[most - 2] = '.';
    out[most] = 0;
}

static void header(void) {
    text(px, GRID_X, 64, 7, "CAPY MOBILE", ACCENT);
    text(px, GRID_X + 2, 132, 3, "JAVA PHONE GAMES", DIM);
    char line[64];
    snprintf(line, sizeof(line), "%d %s", game_count, game_count == 1 ? "GAME" : "GAMES");
    text(px, WIDTH - GRID_X - (int)strlen(line) * 18, 84, 3, line, DIM);
}

static void draw_list(void) {
    fill(0, 0, WIDTH, HEIGHT, BG);
    header();
    if (!game_count) {
        centred(WIDTH / 2, 380, 5, "NO GAMES YET", WHITE);
        centred(WIDTH / 2, 470, 3, "COPY .JAR GAMES INTO THIS FOLDER ON THE PS5, THEN PRESS {TRI}", DIM);
        char where[260];
        snprintf(where, sizeof(where), "/data/homebrew/%s/games", CAPY_MOBILE_TITLE);
        centred(WIDTH / 2, 540, 4, where, ACCENT);
    }
    for (int i = top_row * COLUMNS; i < game_count && i < (top_row + ROWS) * COLUMNS; ++i) {
        const Game *game = &games[i];
        int x = GRID_X + i % COLUMNS * TILE_STEP_X, y = GRID_Y + (i / COLUMNS - top_row) * TILE_STEP_Y;
        fill(x, y, TILE, TILE, PANEL);
        if (game->icon) {
            // The largest whole enlargement that leaves a margin: phone icons are 12 to 64 pixels.
            int most = game->icon_w > game->icon_h ? game->icon_w : game->icon_h, scale = 176 / most;
            if (scale < 1) scale = 1;
            int w = game->icon_w * scale, h = game->icon_h * scale;
            enlarge(game->icon, game->icon_w, game->icon_h, x + (TILE - w) / 2, y + (TILE - h) / 2, w, h, 1);
        } else {
            char letter[2] = {game->name[0] >= 'a' && game->name[0] <= 'z' ? (char)(game->name[0] - 32) : game->name[0], 0};
            text(px, x + TILE / 2 - 35, y + TILE / 2 - 49, 14, letter, DIM);
        }
        char name[32];
        cut(name, sizeof(name), game->name, 21);
        text(px, x + (TILE - (int)strlen(name) * 12) / 2, y + TILE + 16, 2, name, i == chosen ? WHITE : DIM);
        if (i == chosen) frame_around(x, y, TILE, TILE, 6, ACCENT);
    }
    if (game_count) {
        const Game *game = &games[chosen];
        char line[200];
        text(px, GRID_X, 930, 4, game->name, WHITE);
        if (game->maker[0]) snprintf(line, sizeof(line), "%s   %ld KB", game->maker, (game->bytes + 1023) / 1024);
        else snprintf(line, sizeof(line), "%ld KB", (game->bytes + 1023) / 1024);
        text(px, GRID_X, 976, 2, line, DIM);
    }
    if (message[0]) text(px, WIDTH - GRID_X - (int)strlen(message) * 12, 944, 2, message, WARN);
    ui_text(px, GRID_X, 1024, 3, game_count ? "{X} PLAY     {TRI} LOOK FOR NEW GAMES" : "{TRI} LOOK FOR NEW GAMES", DIM);
    char version[48];
    snprintf(version, sizeof(version), "VERSION %s", CAPY_MOBILE_VERSION);
    text(px, WIDTH - GRID_X - (int)strlen(version) * 12, 1030, 2, version, 0x5a6b7c);
    static const char credit[] = "MADE BY KHALIFA007";
    text(px, WIDTH - GRID_X - (int)(sizeof(credit) - 1) * 12, 1002, 2, credit, DIM);
}

// The controller as a phone, shown beside the game and in the pause menu.
static const char *const key_lines[] = {
    "{DPAD}  MOVE", "{X}  OK / FIRE", "{L1}  LEFT KEY", "{R1}  RIGHT KEY", "{SQ} 1     {TRI} 3",
    "{L2} 7     {R2} 9", "{O}  0", "L3  *      R3  #", "{OPT}  MENU",
};
#define KEY_LINES ((int)(sizeof(key_lines) / sizeof(key_lines[0])))

static void draw_keys(int x, int y, int scale, uint32_t color) {
    for (int i = 0; i < KEY_LINES; ++i) ui_text(px, x, y + i * scale * 15, scale, key_lines[i], color);
}

// Where the game's picture goes: the largest whole enlargement that fits, or the full height.
static int tall;
static void picture_place(int w, int h, int *x, int *y, int *dw, int *dh) {
    int scale = WIDTH / w < HEIGHT / h ? WIDTH / w : HEIGHT / h;
    if (scale < 1) scale = 1;
    *dw = w * scale;
    *dh = h * scale;
    if (tall && w * HEIGHT / h <= WIDTH) { *dh = HEIGHT; *dw = w * HEIGHT / h; }
    *x = (WIDTH - *dw) / 2;
    *y = (HEIGHT - *dh) / 2;
}

static void draw_game(const Game *game, int around) {
    int w = 240, h = 320, x, y, dw, dh;
    const uint32_t *picture = core_picture(&w, &h);
    picture_place(w, h, &x, &y, &dw, &dh);
    if (around) {
        // Everything beside the picture changes only with the game or its size, so it is drawn then.
        fill(0, 0, WIDTH, HEIGHT, 0x0a1018);
        if (x >= 420 && around == 1) {  // 2: under the pause menu, which shows the keys itself
            char name[40];
            cut(name, sizeof(name), game->name, 22);
            text(px, 60, 80, 3, name, WHITE);
            if (game->maker[0]) { cut(name, sizeof(name), game->maker, 30); text(px, 60, 124, 2, name, DIM); }
            draw_keys(x + dw + 60, 80, 3, DIM);
        }
        if (!picture) fill(x, y, dw, dh, 0);
    }
    if (picture) enlarge(picture, w, h, x, y, dw, dh, 0);
}

// The phone screens a game can be given. A .jar was built for one size and often for one way up;
// the wrong one shows as a black screen, a cut-off picture or the game's own "turn your phone" note.
// "AUTO" is the size the game's manifest names, else 240x320. (These are sizes the engine knows.)
static const char *const screens[] = {
    "auto", "240x320", "176x220", "176x208", "128x160", "320x240", "640x360", "360x640", "480x800", "800x480",
};
#define SCREENS ((int)(sizeof(screens) / sizeof(screens[0])))

// What was chosen for a game is remembered beside its saves, in "<its file>.set": its phone screen
// and whether it runs at a phone's speed or as fast as the console goes.
typedef struct { int screen, turbo; } Choice;

static Choice saved_choice(const Game *game) {
    Choice choice = {0, 0};
    char path[420], line[40];
    snprintf(path, sizeof(path), "%s/%s.set", saves_folder, game->file);
    FILE *file = fopen(path, "r");
    if (!file) return choice;
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;
        for (int i = 0; i < SCREENS; ++i)
            if (!strncmp(line, "screen=", 7) && !strcmp(line + 7, screens[i])) choice.screen = i;
        if (!strcmp(line, "speed=turbo")) choice.turbo = 1;
    }
    fclose(file);
    return choice;
}

static void save_choice(const Game *game, Choice choice) {
    char path[420];
    snprintf(path, sizeof(path), "%s/%s.set", saves_folder, game->file);
    if (!choice.screen && !choice.turbo) { unlink(path); return; }
    FILE *file = fopen(path, "w");
    if (!file) return;
    fprintf(file, "screen=%s\nspeed=%s\n", screens[choice.screen], choice.turbo ? "turbo" : "phone");
    fclose(file);
}

static const char *const menu_items[] = {"RESUME", "RESTART THE GAME", "PICTURE", "PHONE SCREEN", "SPEED", "BACK TO THE GAMES"};
enum { MENU_RESUME, MENU_RESTART, MENU_SIZE, MENU_SCREEN, MENU_SPEED, MENU_EXIT, MENU_COUNT };

static void draw_menu(const Game *game, int item, Choice choice, int screen_was) {
    int screen = choice.screen;
    int w = 1060, h = 760, x = (WIDTH - w) / 2, y = (HEIGHT - h) / 2;
    fill(x, y, w, h, PANEL);
    frame_around(x, y, w, h, 4, ACCENT);
    char name[48];
    cut(name, sizeof(name), game->name, 34);
    text(px, x + 50, y + 44, 4, name, WHITE);
    for (int i = 0; i < MENU_COUNT; ++i) {
        char line[64];
        if (i == MENU_SIZE) snprintf(line, sizeof(line), "PICTURE: %s", tall ? "FULL HEIGHT" : "SHARP");
        else if (i == MENU_SCREEN) snprintf(line, sizeof(line), "PHONE SCREEN: %s", screen ? screens[screen] : "AUTO");
        else if (i == MENU_SPEED) snprintf(line, sizeof(line), "SPEED: %s", choice.turbo ? "TURBO" : "PHONE");
        else snprintf(line, sizeof(line), "%s", menu_items[i]);
        if (i == item) fill(x + 40, y + 122 + i * 62, 560, 52, 0x2a3d50);
        text(px, x + 56, y + 134 + i * 62, 4, line, i == item ? ACCENT : WHITE);
    }
    if (item == MENU_SCREEN) {
        text(px, x + 56, y + 134 + MENU_COUNT * 62 + 16, 2, "THE SIZE THE GAME WAS MADE FOR. TRY ANOTHER IF", DIM);
        text(px, x + 56, y + 134 + MENU_COUNT * 62 + 42, 2, "THE PICTURE IS BLACK, CUT OFF OR SIDEWAYS.", DIM);
        if (screen != screen_was) text(px, x + 56, y + 134 + MENU_COUNT * 62 + 80, 2, "THE GAME STARTS AGAIN WITH THE NEW SCREEN.", WARN);
    }
    if (item == MENU_SPEED) {
        text(px, x + 56, y + 134 + MENU_COUNT * 62 + 16, 2, "PHONE: AS FAST AS A PHONE OF ITS TIME.", DIM);
        text(px, x + 56, y + 134 + MENU_COUNT * 62 + 42, 2, "TURBO: SHORTER LOADING; SOME GAMES RUN TOO FAST.", DIM);
    }
    text(px, x + 660, y + 134, 2, "THE CONTROLLER AS A PHONE", DIM);
    draw_keys(x + 660, y + 176, 3, WHITE);
    ui_text(px, x + 50, y + h - 60, 3, item == MENU_SCREEN ? "{DPAD} CHANGE     {O} RESUME" : "{X} CHOOSE     {O} RESUME", DIM);
}

// ---------- the controller ----------

static uint32_t phone_keys(uint32_t pad) {
    uint32_t keys = 0;
    if (pad & PAD_UP) keys |= KEY_UP;
    if (pad & PAD_DOWN) keys |= KEY_DOWN;
    if (pad & PAD_LEFT) keys |= KEY_LEFT;
    if (pad & PAD_RIGHT) keys |= KEY_RIGHT;
    if (pad & PAD_CROSS) keys |= KEY_FIRE;
    if (pad & PAD_L1) keys |= KEY_SOFT_LEFT;
    if (pad & PAD_R1) keys |= KEY_SOFT_RIGHT;
    if (pad & PAD_SQUARE) keys |= KEY_DIGIT(1);
    if (pad & PAD_TRIANGLE) keys |= KEY_DIGIT(3);
    if (pad & PAD_L2) keys |= KEY_DIGIT(7);
    if (pad & PAD_R2) keys |= KEY_DIGIT(9);
    if (pad & PAD_CIRCLE) keys |= KEY_DIGIT(0);
    if (pad & PAD_L3) keys |= KEY_STAR;
    if (pad & PAD_R3) keys |= KEY_HASH;
    return keys;
}

// A held arrow moves again after 0.4 s, then eight times a second.
static uint32_t repeated(uint32_t pad, uint32_t previous) {
    static uint64_t since, last;
    uint32_t pressed = pad & ~previous, arrows = pad & PAD_ARROWS;
    uint64_t now = now_us();
    if (pressed & PAD_ARROWS || !arrows) { since = last = now; return pressed; }
    if (now - since > 400000 && now - last > 125000) { last = now; pressed |= arrows; }
    return pressed;
}

// ---------- the game ----------

static void play(Game *game) {
    char path[400];
    snprintf(path, sizeof(path), "%s/%s", games_folder, game->file);
    px = begin_frame();
    fill(0, 0, WIDTH, HEIGHT, 0x0a1018);
    centred(WIDTH / 2, 500, 4, "STARTING...", DIM);
    end_frame();
    message[0] = 0;
    Choice choice = saved_choice(game);
    int screen_was = choice.screen;
    log_line("game: %s (%s, %ld bytes), screen %s, speed %s", game->file, game->name, game->bytes, screens[choice.screen],
             choice.turbo ? "turbo" : "phone");
    core_screen(screens[choice.screen]);
    core_speed(choice.turbo);
    if (core_load(path)) {
        snprintf(message, sizeof(message), "%.60s DID NOT START", game->name);
        log_line("game: it did not start: %s", core_problem());
        return;
    }
    int menu = 0, item = 0, around = 1, shown_w = 0, shown_h = 0;
    uint32_t previous = read_buttons();
    uint64_t started = now_us();
    unsigned frames = 0;
    for (;;) {
        uint32_t pad = read_buttons(), pressed = menu ? repeated(pad, previous) : pad & ~previous;
        previous = pad;
        px = begin_frame();
        if (!menu) {
            if (pressed & PAD_OPTIONS) { menu = 1; item = 0; audio_quiet(); continue; }
            core_run(phone_keys(pad));
            ++frames;
            if (core_ended()) break;
            int w = 0, h = 0;
            if (core_picture(&w, &h) && (w != shown_w || h != shown_h)) { shown_w = w; shown_h = h; around = 1; }
            draw_game(game, around);
            around = 0;
        } else {
            if (pressed & PAD_UP) item = (item + MENU_COUNT - 1) % MENU_COUNT;
            if (pressed & PAD_DOWN) item = (item + 1) % MENU_COUNT;
            int close_menu = (pressed & (PAD_CIRCLE | PAD_OPTIONS)) != 0, restart = 0;
            if (item == MENU_SCREEN) {
                if (pressed & (PAD_RIGHT | PAD_CROSS)) choice.screen = (choice.screen + 1) % SCREENS;
                if (pressed & PAD_LEFT) choice.screen = (choice.screen + SCREENS - 1) % SCREENS;
            } else if (item == MENU_SPEED) {
                // The speed changes at once, in the running game.
                if (pressed & (PAD_RIGHT | PAD_LEFT | PAD_CROSS)) {
                    choice.turbo = !choice.turbo;
                    core_speed(choice.turbo);
                    save_choice(game, (Choice){screen_was, choice.turbo});
                    log_line("game: speed %s", choice.turbo ? "turbo" : "phone");
                }
            } else if (pressed & PAD_CROSS) {
                if (item == MENU_RESUME) close_menu = 1;
                if (item == MENU_SIZE) tall = !tall;
                if (item == MENU_EXIT) break;
                if (item == MENU_RESTART) restart = close_menu = 1;
            }
            // Leaving the menu with another phone screen chosen starts the game again on it.
            if (close_menu && choice.screen != screen_was) {
                save_choice(game, choice);
                log_line("game: screen %s", screens[choice.screen]);
                screen_was = choice.screen;
                restart = 1;
            }
            if (restart) {
                core_unload();
                core_screen(screens[choice.screen]);
                if (core_load(path)) { snprintf(message, sizeof(message), "%.60s DID NOT START", game->name); return; }
                shown_w = shown_h = 0;
            }
            draw_game(game, 2);
            if (close_menu) {
                // The buttons that closed the menu are not the game's: they count as held until let go.
                menu = 0;
                around = 1;
                previous = pad;
                while (read_buttons() & (PAD_CROSS | PAD_CIRCLE | PAD_OPTIONS)) { px = begin_frame(); end_frame(); }
            } else {
                draw_menu(game, item, choice, screen_was);
            }
        }
        end_frame();
    }
    if (core_ended() && core_problem()[0]) snprintf(message, sizeof(message), "%.60s STOPPED", game->name);
    log_line("game: closed after %u frames in %u s%s%s", frames, (unsigned)((now_us() - started) / 1000000),
             core_problem()[0] ? ": " : "", core_problem());
    core_unload();
    audio_quiet();
}

// ---------- start ----------

static void *app(void *unused) {
    (void)unused;
    int screen = screen_open();
    if (screen) {
        char line[80];
        snprintf(line, sizeof(line), "Capy Mobile: the screen did not open (step %d)", -screen);
        notify(line);
        for (;;) sleep_ms(1000);
    }
    px = begin_frame();
    fill(0, 0, WIDTH, HEIGHT, BG);
    header();
    end_frame();
    pad_open();

#ifdef __linux__
    // The engine's switches, which the console build answers from the same table (src/ps5/libc_port.c).
    for (size_t i = 0; i < sizeof(engine_env) / sizeof(engine_env[0]); ++i) setenv(engine_env[i].name, engine_env[i].value, 0);
    const char *home = getenv("CAPY_MOBILE_HOME") ? getenv("CAPY_MOBILE_HOME") : ".";
#else
    // The app's own folder on the console, /data/homebrew/<title ID>: games arrive there over FTP.
    const char *home = "/app0";
#endif
    snprintf(games_folder, sizeof(games_folder), "%s/games", home);
    snprintf(saves_folder, sizeof(saves_folder), "%s/saves", home);
    char log_path[240] = "";
    int own = writable(saves_folder);
    if (own) {
        mkdir(games_folder, 0777);
        snprintf(log_path, sizeof(log_path), "%s/capy-mobile.log", home);
    } else {
        // A read-only app folder: the saves go to the title's private storage instead.
        static const char *const places[] = {"/download0", "/data/capy-mobile"};
        for (int i = 0; i < 2 && !log_path[0]; ++i) {
            mkdir(places[i], 0777);
            snprintf(saves_folder, sizeof(saves_folder), "%s/saves", places[i]);
            if (writable(saves_folder)) snprintf(log_path, sizeof(log_path), "%s/capy-mobile.log", places[i]);
        }
    }
    log_open(log_path[0] ? log_path : "/download0/capy-mobile.log", now_us, "Capy Mobile " CAPY_MOBILE_VERSION " started");
    char line[160];
    firmware_text(line, sizeof(line));
    log_line("console: system software %s, controller %s", line[0] ? line : "unknown", ui_user >= 0 ? "open" : "not open");
    log_line("folders: games in %s, saves in %s%s", games_folder, saves_folder, own ? "" : " (the app's folder is read-only)");
#ifndef __linux__
    size_t cj_heap_size(void);
    log_line("memory: the app's heap is %u MB, %ld MB of the title's memory are left", (unsigned)(cj_heap_size() >> 20), big_free_megabytes());
#endif

    audio_open();
    core_open(saves_folder, audio_push);
    log_line("engine: ready");
    scan_games();
    audio_status(line, sizeof(line));
    log_line("sound: %s", line);
    log_line("screen: a frame takes %u us to show", frame_cost_us());

    uint32_t previous = read_buttons();
    for (;;) {
        uint32_t pad = read_buttons(), pressed = repeated(pad, previous);
        previous = pad;
        int before = chosen;
        if (game_count) {
            if (pressed & PAD_LEFT && chosen % COLUMNS) --chosen;
            if (pressed & PAD_RIGHT && chosen % COLUMNS < COLUMNS - 1 && chosen + 1 < game_count) ++chosen;
            if (pressed & PAD_UP && chosen >= COLUMNS) chosen -= COLUMNS;
            if (pressed & PAD_DOWN && chosen / COLUMNS < (game_count - 1) / COLUMNS)
                chosen = chosen + COLUMNS < game_count ? chosen + COLUMNS : game_count - 1;
            if (chosen != before) message[0] = 0;
            if (chosen / COLUMNS < top_row) top_row = chosen / COLUMNS;
            if (chosen / COLUMNS >= top_row + ROWS) top_row = chosen / COLUMNS - ROWS + 1;
            if (pressed & PAD_CROSS) {
                play(&games[chosen]);
                previous = read_buttons() | PAD_CROSS | PAD_CIRCLE;
                continue;
            }
        }
        if (pressed & PAD_TRIANGLE) {
            scan_games();
            top_row = chosen / COLUMNS >= ROWS ? chosen / COLUMNS - ROWS + 1 : 0;
            snprintf(message, sizeof(message), "%d %s FOUND", game_count, game_count == 1 ? "GAME" : "GAMES");
        }
        px = begin_frame();
        draw_list();
        end_frame();
    }
}

// The app runs on a thread of its own with a large stack: the engine interprets a game's first thread
// on the caller's stack, a deep one for some games, and a title's main thread has little to spare.
int main(void) {
    pthread_t thread;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 16u << 20);
    if (pthread_create(&thread, &attributes, app, NULL)) app(NULL);
    else pthread_join(thread, NULL);
    return 0;
}
