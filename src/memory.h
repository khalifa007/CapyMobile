#pragma once
// Large blocks of memory straight from the console: the title's flexible memory, in whole 16 KiB
// pages. The screen's picture is one. (Everything else comes from the app's own heap, ps5/alloc.c,
// which is itself one such block.)
#include <stddef.h>

// size bytes of zeroed memory, or NULL. Meant for blocks of 64 KiB and more.
void *big_alloc(size_t size);
// Gives a block back; size is what it was asked for with.
void big_free(void *block, size_t size);
// Megabytes of flexible memory still free, or -1 when the console does not say.
long big_free_megabytes(void);
