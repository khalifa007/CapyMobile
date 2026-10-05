#pragma once
// getcontext(), setcontext(), swapcontext() and makecontext() for x86-64, for the Java engine's
// cooperative threads (its threads.c), in place of the console's own. A PS5 title's are a system call
// each and nobody had tried them in one; these are a few instructions and the same code runs on the PC,
// where the simulator can check it (PORTED=1 in tests/run.sh). The engine's Switch build does the same
// for aarch64 (its switch_ucontext.h), and like that one this keeps only what the scheduler uses: the
// registers a function must preserve, the stack pointer and where to go on; no signal mask, and
// makecontext() takes no arguments for the entry function.
#include <stddef.h>

typedef struct { void *ss_sp; size_t ss_size; int ss_flags; } capy_stack_t;

typedef struct capy_ucontext {
    // capy_ucontext.c's assembly knows these offsets.
    void *rbx, *rbp, *r12, *r13, *r14, *r15, *rsp, *rip;  // 0 to 56
    unsigned int mxcsr;                                   // 64
    unsigned short fpucw, pad;                            // 68
    capy_stack_t uc_stack;
    struct capy_ucontext *uc_link;                        // Where to go when the entry function returns
} capy_ucontext_t;

int capy_getcontext(capy_ucontext_t *context);
int capy_setcontext(const capy_ucontext_t *context);
int capy_swapcontext(capy_ucontext_t *save, const capy_ucontext_t *resume);
void capy_makecontext(capy_ucontext_t *context, void (*entry)(void), int argc, ...);

#define ucontext_t capy_ucontext_t
#define getcontext capy_getcontext
#define setcontext capy_setcontext
#define swapcontext capy_swapcontext
#define makecontext capy_makecontext
