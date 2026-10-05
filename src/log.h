#pragma once
// The app's log: what it did on this launch, in memory and in a file that can be read over FTP
// (capy-mobile.log in the app's folder; the launch before is capy-mobile.log.1). No console includes,
// so the PC tests build it too.
#include <stddef.h>
#include <stdint.h>

// Starts the log at path, keeping the file from the launch before as path.1, and writes first_line.
// now_us gives the time for each line's "+12.345s" prefix (NULL: the wall clock, whole seconds).
void log_open(const char *path, uint64_t (*now_us)(void), const char *first_line);
// Carries the log on in another file, which starts with what this launch has logged so far; that
// file's copy from the launch before is kept as path.1. For a folder that opens only after start-up
// (the PS5 app's /data).
void log_move(const char *path);
// Appends one line (no newline needed). Safe from any thread. Never fails: without a file the memory
// copy still grows. Never pass a password or a token.
void log_line(const char *format, ...);
// Copies the newest part of this launch's log into out (whole lines, at most cap-1 bytes) and returns
// its length. Works with nothing open too (then it is empty).
size_t log_tail(char *out, size_t cap);
// The end of the launch before (path.1 as log_open found it), or "" when there was none.
size_t log_previous(char *out, size_t cap);
