// The context switch of capy_ucontext.h (x86-64, System V calling convention).
#undef ucontext_t
#include <stddef.h>
#include <stdint.h>
#include "capy_ucontext.h"

_Static_assert(offsetof(capy_ucontext_t, rsp) == 48 && offsetof(capy_ucontext_t, rip) == 56, "the assembly's offsets");
_Static_assert(offsetof(capy_ucontext_t, mxcsr) == 64 && offsetof(capy_ucontext_t, fpucw) == 68, "the assembly's offsets");

// Saving: the registers a called function must give back unchanged, the stack as it will be once this
// call has returned, and the address it returns to. Resuming a saved context therefore looks like that
// call returning 0 once more.
__asm__(
    ".text\n"
    ".globl capy_getcontext\n"
    "capy_getcontext:\n"
    "    movq %rbx, 0(%rdi)\n"
    "    movq %rbp, 8(%rdi)\n"
    "    movq %r12, 16(%rdi)\n"
    "    movq %r13, 24(%rdi)\n"
    "    movq %r14, 32(%rdi)\n"
    "    movq %r15, 40(%rdi)\n"
    "    leaq 8(%rsp), %rax\n"
    "    movq %rax, 48(%rdi)\n"
    "    movq (%rsp), %rax\n"
    "    movq %rax, 56(%rdi)\n"
    "    stmxcsr 64(%rdi)\n"
    "    fnstcw 68(%rdi)\n"
    "    xorl %eax, %eax\n"
    "    ret\n"
    ".globl capy_swapcontext\n"
    "capy_swapcontext:\n"
    "    movq %rbx, 0(%rdi)\n"
    "    movq %rbp, 8(%rdi)\n"
    "    movq %r12, 16(%rdi)\n"
    "    movq %r13, 24(%rdi)\n"
    "    movq %r14, 32(%rdi)\n"
    "    movq %r15, 40(%rdi)\n"
    "    leaq 8(%rsp), %rax\n"
    "    movq %rax, 48(%rdi)\n"
    "    movq (%rsp), %rax\n"
    "    movq %rax, 56(%rdi)\n"
    "    stmxcsr 64(%rdi)\n"
    "    fnstcw 68(%rdi)\n"
    "    movq %rsi, %rdi\n"
    ".globl capy_setcontext\n"
    "capy_setcontext:\n"
    "    movq 0(%rdi), %rbx\n"
    "    movq 8(%rdi), %rbp\n"
    "    movq 16(%rdi), %r12\n"
    "    movq 24(%rdi), %r13\n"
    "    movq 32(%rdi), %r14\n"
    "    movq 40(%rdi), %r15\n"
    "    ldmxcsr 64(%rdi)\n"
    "    fldcw 68(%rdi)\n"
    "    movq 48(%rdi), %rsp\n"
    "    movq 56(%rdi), %rdx\n"
    "    xorl %eax, %eax\n"
    "    jmp *%rdx\n"
    // Where a context's entry function returns to: its context is still in r12, where
    // capy_makecontext() put it, because every function gives r12 back unchanged.
    "capy_context_return:\n"
    "    movq %r12, %rdi\n"
    "    call capy_context_finished\n"
    "    ud2\n"
);

void capy_context_return(void);

void capy_context_finished(capy_ucontext_t *context) {
    if (context->uc_link) capy_setcontext(context->uc_link);
    __builtin_trap();  // Nowhere to go on
}

void capy_makecontext(capy_ucontext_t *context, void (*entry)(void), int argc, ...) {
    if (argc) __builtin_trap();
    // A function starts with the stack as a call leaves it: 16-byte aligned plus the return address.
    void **top = (void **)(((uintptr_t)context->uc_stack.ss_sp + context->uc_stack.ss_size) & ~(uintptr_t)15);
    *--top = (void *)capy_context_return;
    context->rsp = top;
    context->rip = (void *)entry;
    context->r12 = context;
    context->rbx = context->rbp = context->r13 = context->r14 = context->r15 = NULL;
    context->mxcsr = 0x1f80;  // The defaults: all floating-point exceptions masked, round to nearest
    context->fpucw = 0x037f;
}
