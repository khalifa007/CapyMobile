// Thread-local variables on the console. The PS5 toolchain compiles "__thread" and "_Thread_local" as
// calls to __emutls_get_address() (-femulated-tls) and the title's runtime does not have that function,
// so a title that uses one does not link. The Java engine uses dozens, so here it is:
// every such variable has a small control block the compiler fills in (size, alignment, initial value),
// and each thread gets its own copy the first time it touches the variable.
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// What the compiler emits for one thread-local variable (LLVM's and GCC's emutls layout).
typedef struct {
    size_t size, align;
    union { uintptr_t index; void *address; } object;  // 0 until the variable has a number
    void *initial;                                     // NULL: starts as zeroes
} Control;

typedef struct { size_t count; void **slots; } Copies;

static pthread_key_t key;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t numbering = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t numbered;

static void thread_ends(void *value) {
    Copies *copies = value;
    for (size_t i = 0; i < copies->count; ++i) free(copies->slots[i]);
    free(copies->slots);
    free(copies);
}

static void start(void) { pthread_key_create(&key, thread_ends); }

void *__emutls_get_address(Control *control) {
    uintptr_t index = __atomic_load_n(&control->object.index, __ATOMIC_ACQUIRE);
    if (!index) {
        pthread_once(&once, start);
        pthread_mutex_lock(&numbering);
        index = control->object.index;
        if (!index) {
            index = ++numbered;
            __atomic_store_n(&control->object.index, index, __ATOMIC_RELEASE);
        }
        pthread_mutex_unlock(&numbering);
    }
    Copies *copies = pthread_getspecific(key);
    if (!copies) {
        copies = calloc(1, sizeof(*copies));
        if (!copies) __builtin_trap();
        pthread_setspecific(key, copies);
    }
    if (index > copies->count) {
        size_t count = index + 32;
        void **slots = realloc(copies->slots, count * sizeof(void *));
        if (!slots) __builtin_trap();
        memset(slots + copies->count, 0, (count - copies->count) * sizeof(void *));
        copies->slots = slots;
        copies->count = count;
    }
    void *copy = copies->slots[index - 1];
    if (!copy) {
        size_t align = control->align < sizeof(void *) ? sizeof(void *) : control->align;
        if (posix_memalign(&copy, align, control->size ? control->size : 1)) __builtin_trap();
        if (control->initial) memcpy(copy, control->initial, control->size);
        else memset(copy, 0, control->size);
        copies->slots[index - 1] = copy;
    }
    return copy;
}
