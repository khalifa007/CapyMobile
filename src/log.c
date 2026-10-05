#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static SRWLOCK lock = SRWLOCK_INIT;
static void lock_log(void) { AcquireSRWLockExclusive(&lock); }
static void unlock_log(void) { ReleaseSRWLockExclusive(&lock); }
#else
#include <pthread.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void lock_log(void) { pthread_mutex_lock(&lock); }
static void unlock_log(void) { pthread_mutex_unlock(&lock); }
#endif

// The memory copy keeps this much; the file stops growing at LOG_FILE_MAX so a long session cannot
// fill the console's storage.
#define LOG_KEEP (32 * 1024)
#define LOG_FILE_MAX (2 * 1024 * 1024)

static char ring[LOG_KEEP];
static size_t ring_len;
static char path[256], previous_path[264];
static uint64_t (*clock_us)(void);
static uint64_t opened_us;
static time_t opened_s;
static size_t file_written;
static int file_off;  // Set once the file could not be opened; the memory copy goes on alone.

static void stamp(char *out, size_t cap) {
    if (clock_us) {
        uint64_t t = clock_us() - opened_us;
        snprintf(out, cap, "+%llu.%03llus", (unsigned long long)(t / 1000000), (unsigned long long)(t / 1000 % 1000));
    } else {
        snprintf(out, cap, "+%llus", (unsigned long long)(time(NULL) - opened_s));
    }
}

static void keep(const char *line, size_t n) {
    if (n + 1 > LOG_KEEP) { line += n + 1 - LOG_KEEP; n = LOG_KEEP - 1; }
    if (ring_len + n + 1 > LOG_KEEP) {
        // Drop the oldest quarter, to the end of a line, so the copy stays made of whole lines.
        size_t cut = LOG_KEEP / 4 + n;
        if (cut > ring_len) cut = ring_len;
        while (cut < ring_len && ring[cut - 1] != '\n') cut++;
        memmove(ring, ring + cut, ring_len - cut);
        ring_len -= cut;
    }
    memcpy(ring + ring_len, line, n);
    ring_len += n;
    ring[ring_len++] = '\n';
}

static void write_file(const char *line, size_t n) {
    if (!path[0] || file_off || file_written + n + 1 > LOG_FILE_MAX) return;
    FILE *f = fopen(path, "ab");
    if (!f) { file_off = 1; return; }
    fwrite(line, 1, n, f);
    fputc('\n', f);
    fclose(f);
    file_written += n + 1;
}

void log_open(const char *at, uint64_t (*now_us)(void), const char *first_line) {
    lock_log();
    snprintf(path, sizeof(path), "%s", at);
    snprintf(previous_path, sizeof(previous_path), "%s.1", at);
    clock_us = now_us;
    opened_us = now_us ? now_us() : 0;
    opened_s = time(NULL);
    ring_len = 0;
    file_written = 0;
    file_off = 0;
    // The launch before stays readable as .1 (a failure here only loses that copy).
    remove(previous_path);
    rename(path, previous_path);
    FILE *f = fopen(path, "wb");
    if (f) fclose(f);
    else file_off = 1;
    unlock_log();
    if (first_line) log_line("%s", first_line);
}

void log_move(const char *at) {
    lock_log();
    snprintf(path, sizeof(path), "%s", at);
    snprintf(previous_path, sizeof(previous_path), "%s.1", at);
    file_written = 0;
    file_off = 0;
    remove(previous_path);
    rename(path, previous_path);
    FILE *f = fopen(path, "wb");
    if (f) {
        // What this launch has logged so far, so the new file starts at the beginning.
        file_written = fwrite(ring, 1, ring_len, f);
        fclose(f);
    } else file_off = 1;
    unlock_log();
}

void log_line(const char *format, ...) {
    char text[1024], line[1100], when[32];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    lock_log();
    stamp(when, sizeof(when));
    int n = snprintf(line, sizeof(line), "%s %s", when, text);
    if (n < 0) n = 0;
    if ((size_t)n >= sizeof(line)) n = sizeof(line) - 1;
    keep(line, (size_t)n);
    write_file(line, (size_t)n);
    unlock_log();
}

size_t log_tail(char *out, size_t cap) {
    if (!cap) return 0;
    lock_log();
    size_t n = ring_len < cap - 1 ? ring_len : cap - 1, start = ring_len - n;
    // A cut in the middle of a line starts at the next one instead.
    while (start < ring_len && start > 0 && ring[start - 1] != '\n') start++;
    n = ring_len - start;
    memcpy(out, ring + start, n);
    out[n] = 0;
    unlock_log();
    return n;
}

size_t log_previous(char *out, size_t cap) {
    if (!cap) return 0;
    out[0] = 0;
    if (!previous_path[0]) return 0;
    FILE *f = fopen(previous_path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 0; }
    long size = ftell(f), from = size > (long)(cap - 1) ? size - (long)(cap - 1) : 0;
    if (size <= 0 || fseek(f, from, SEEK_SET)) { fclose(f); return 0; }
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = 0;
    // Start at a whole line unless the file is small enough to fit entirely.
    char *first = from ? strchr(out, '\n') : NULL;
    if (first) {
        n -= (size_t)(first + 1 - out);
        memmove(out, first + 1, n + 1);
    }
    return n;
}
