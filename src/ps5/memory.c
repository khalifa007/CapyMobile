// Large blocks for the PS5 app (memory.h). On the console they are flexible memory, mapped read/write
// (PS5SX2 gets its memory the same way: plain mmap() gives an app nothing). If the console refuses,
// the block is tried on the heap, so a caller only ever has to check for NULL. On Linux, for the PC
// tests, they are ordinary heap blocks.
#include "memory.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#ifndef __linux__
#define PAGE 0x4000u

int sceKernelMapFlexibleMemory(void **address, size_t length, int protection, int flags);
int sceKernelReleaseFlexibleMemory(void *address, size_t length);
int sceKernelAvailableFlexibleMemorySize(size_t *size);

// The blocks that came from the heap instead, so big_free() gives each back to where it came from.
static void *from_heap[16];
static pthread_mutex_t heap_lock = PTHREAD_MUTEX_INITIALIZER;

void *big_alloc(size_t size) {
    void *block = NULL;
    size_t length = (size + PAGE - 1) & ~(size_t)(PAGE - 1);
    if (length && !sceKernelMapFlexibleMemory(&block, length, 3, 0) && block) return block;  // Mapped pages are zero.
    block = calloc(1, size);
    pthread_mutex_lock(&heap_lock);
    int kept = 0;
    for (int i = 0; block && i < 16 && !kept; ++i)
        if (!from_heap[i]) { from_heap[i] = block; kept = 1; }
    pthread_mutex_unlock(&heap_lock);
    if (!kept) { free(block); block = NULL; }
    return block;
}

void big_free(void *block, size_t size) {
    if (!block) return;
    pthread_mutex_lock(&heap_lock);
    int heap = 0;
    for (int i = 0; i < 16 && !heap; ++i)
        if (from_heap[i] == block) { from_heap[i] = NULL; heap = 1; }
    pthread_mutex_unlock(&heap_lock);
    if (heap) { free(block); return; }
    sceKernelReleaseFlexibleMemory(block, (size + PAGE - 1) & ~(size_t)(PAGE - 1));
}

long big_free_megabytes(void) {
    size_t size = 0;
    return sceKernelAvailableFlexibleMemorySize(&size) ? -1 : (long)(size >> 20);
}
#else
void *big_alloc(size_t size) { return calloc(1, size); }
void big_free(void *block, size_t size) { (void)size; free(block); }
long big_free_megabytes(void) { return -1; }
#endif
