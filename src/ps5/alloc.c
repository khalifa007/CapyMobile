// The app's heap on the console. malloc() in a PS5 title is the system's small internal heap, which
// runs out long before the process does, and a Java engine allocates all the time. So one large block
// of the title's flexible memory is taken at the first allocation and Doug Lea's allocator
// (third_party/dlmalloc.c, public domain) hands it out.
//
// The build defines malloc, free, calloc, realloc, strdup and friends as the cj_ names below for every
// source of the app and the engine, so nothing has to be changed to use it. Memory that the console's
// own library allocated (it calls its own malloc) may still be handed to free(): a pointer outside the
// block goes back to that library.
#undef malloc
#undef free
#undef calloc
#undef realloc
#undef strdup
#undef strndup
#undef posix_memalign
#undef aligned_alloc
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int sceKernelMapFlexibleMemory(void **address, size_t length, int protection, int flags);

#define ONLY_MSPACES 1
#define HAVE_MMAP 0
#define HAVE_MORECORE 0
#define USE_LOCKS 1
#define USE_SPIN_LOCKS 1
#define NO_MALLOC_STATS 1
#define NO_MALLINFO 1
#define LACKS_SYS_MMAN_H 1
#define LACKS_SYS_PARAM_H 1
#define LACKS_UNISTD_H 1
#define LACKS_TIME_H 1
#define malloc_getpagesize 16384
#define ABORT __builtin_trap()
#define MALLOC_FAILURE_ACTION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "dlmalloc.inc"
#pragma clang diagnostic pop

static mspace space;
static uintptr_t low, high;
static size_t taken;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void start(void) {
    // The title has about 448 MiB of flexible memory. The screen's picture needs 8 of them and the
    // stacks of the app's and the games' threads come out of it too, so the heap leaves room.
    static const size_t sizes[] = {256u << 20, 192u << 20, 128u << 20, 64u << 20};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]) && !space; ++i) {
        void *block = NULL;
        if (sceKernelMapFlexibleMemory(&block, sizes[i], 3, 0) || !block) continue;
        space = create_mspace_with_base(block, sizes[i], 1);
        if (!space) continue;
        low = (uintptr_t)block;
        high = low + sizes[i];
        taken = sizes[i];
    }
}

static int ours(const void *p) { return (uintptr_t)p >= low && (uintptr_t)p < high; }

// For the log: how large the app's heap is and how much of it is in use, in bytes.
size_t cj_heap_size(void) { pthread_once(&once, start); return taken; }
size_t cj_heap_used(void) { return space ? mspace_footprint(space) : 0; }

void *cj_malloc(size_t size) {
    pthread_once(&once, start);
    void *p = space ? mspace_malloc(space, size) : NULL;
    return p ? p : malloc(size);
}

void *cj_calloc(size_t count, size_t size) {
    pthread_once(&once, start);
    void *p = space ? mspace_calloc(space, count, size) : NULL;
    return p ? p : calloc(count, size);
}

void cj_free(void *p) {
    if (!p) return;
    if (ours(p)) mspace_free(space, p);
    else free(p);
}

void *cj_realloc(void *p, size_t size) {
    if (!p) return cj_malloc(size);
    if (!ours(p)) return realloc(p, size);
    if (!size) { mspace_free(space, p); return NULL; }
    void *q = mspace_realloc(space, p, size);
    if (q) return q;
    // The block is full: the bytes move to the console's heap, if it has the room.
    q = malloc(size);
    if (q) {
        size_t had = mspace_usable_size(p);
        memcpy(q, p, had < size ? had : size);
        mspace_free(space, p);
    }
    return q;
}

int cj_posix_memalign(void **out, size_t alignment, size_t size) {
    pthread_once(&once, start);
    if (alignment < sizeof(void *) || (alignment & (alignment - 1))) return EINVAL;
    void *p = space ? mspace_memalign(space, alignment, size) : NULL;
    if (!p) return posix_memalign(out, alignment, size);
    *out = p;
    return 0;
}

void *cj_aligned_alloc(size_t alignment, size_t size) {
    void *p = NULL;
    return cj_posix_memalign(&p, alignment < sizeof(void *) ? sizeof(void *) : alignment, size) ? NULL : p;
}

char *cj_strndup(const char *text, size_t most) {
    size_t length = 0;
    while (length < most && text[length]) ++length;
    char *copy = cj_malloc(length + 1);
    if (copy) { memcpy(copy, text, length); copy[length] = 0; }
    return copy;
}

char *cj_strdup(const char *text) { return cj_strndup(text, (size_t)-1); }
