/* The contexts on Windows (context.h): the switch is the port's own code (context_x64.asm); each
 * context runs on a stack the port allocates, in the game's memory when the runtime provides it, and
 * the stack bounds Windows keeps for the thread are switched with it, as for a fiber. */
#include "context.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

#if !defined(_M_X64)
#error "context_win.c: a switch for x64 (context_x64.asm) only"
#endif

struct KhdaysContext {
    void *sp;             /* its stack pointer where it stopped (khdays_context_jump) */
    unsigned char *stack; /* its stack's lowest address; NULL: the thread's own stack */
    size_t size;
    void (*release)(void *stack, size_t size); /* how its stack is given back */
    void (*entry)(void *argument);
    void *argument;
};

void khdays_context_jump(void **save_sp, void *load_sp);
void khdays_context_trampoline(void);

/* The game's threads all switch on one host thread, so one "current" is enough. */
static KhdaysContext *current;

static void *(*allocate_stack)(size_t size);
static void (*release_stack)(void *stack, size_t size);

void khdays_context_stack_source(void *(*allocate)(size_t size), void (*release)(void *stack, size_t size))
{
    allocate_stack = allocate;
    release_stack = release;
}

/* A stack in the host's own memory, with a guard page below it to catch an overflow. */
static void *host_stack(size_t size)
{
    unsigned char *base = (unsigned char *)VirtualAlloc(NULL, size + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD old;
    if (base == NULL) {
        return NULL;
    }
    VirtualProtect(base, 0x1000, PAGE_NOACCESS, &old);
    return base + 0x1000;
}

static void host_release(void *stack, size_t size)
{
    (void)size;
    VirtualFree((unsigned char *)stack - 0x1000, 0, MEM_RELEASE);
}

/* A context's stack size: what the executable asks for its main thread (its
 * header's stack reserve), as a fiber's default is. */
static size_t stack_size(void)
{
    extern IMAGE_DOS_HEADER __ImageBase;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const unsigned char *)&__ImageBase + __ImageBase.e_lfanew);
    return (nt->OptionalHeader.SizeOfStackReserve + 0xffff) & ~(size_t)0xffff;
}

void khdays_context_started(KhdaysContext *context)
{
    context->entry(context->argument);
    /* an entry must never return (context.h): stop the process at once */
    RaiseFailFastException(NULL, NULL, 0);
}

KhdaysContext *khdays_context_init(void)
{
    KhdaysContext *context = (KhdaysContext *)calloc(1, sizeof(*context));
    if (context != NULL) {
        current = context;
    }
    return context;
}

KhdaysContext *khdays_context_current(void)
{
    return current;
}

KhdaysContext *khdays_context_create(void (*entry)(void *argument), void *argument)
{
    KhdaysContext *context = (KhdaysContext *)calloc(1, sizeof(*context));
    unsigned long long *frame;
    unsigned char *saved;
    if (context == NULL) {
        return NULL;
    }
    context->size = stack_size();
    context->stack = (unsigned char *)(allocate_stack ? allocate_stack(context->size) : host_stack(context->size));
    context->release = allocate_stack ? release_stack : host_release;
    if (context->stack == NULL) {
        free(context);
        return NULL;
    }
    context->entry = entry;
    context->argument = argument;
    /* Build the frame the first switch to it pops (context_x64.asm): it
     * returns into the trampoline, which starts the context. */
    frame = (unsigned long long *)(context->stack + context->size);
    *--frame = 0; /* nothing above the first frame */
    *--frame = 0; /* the return slot 8 below a 16-byte boundary, as a call's */
    *--frame = (unsigned long long)khdays_context_trampoline;
    *--frame = 0;                           /* rbp */
    *--frame = (unsigned long long)context; /* rbx: the trampoline's argument */
    *--frame = 0;                           /* rdi */
    *--frame = 0;                           /* rsi */
    *--frame = 0;                           /* r12 */
    *--frame = 0;                           /* r13 */
    *--frame = 0;                           /* r14 */
    *--frame = 0;                           /* r15 */
    frame -= 168 / 8;                       /* xmm6-xmm15, MXCSR, the x87 control word */
    saved = (unsigned char *)frame;
    memset(saved, 0, 168);
    *(unsigned int *)(saved + 160) = 0x1f80;   /* MXCSR: every exception masked, round to nearest */
    *(unsigned short *)(saved + 164) = 0x027f; /* x87: masked, 53-bit precision (Windows x64's) */
    *--frame = (unsigned long long)(context->stack + context->size); /* StackBase */
    *--frame = (unsigned long long)context->stack;                   /* StackLimit */
    *--frame = (unsigned long long)context->stack;                   /* DeallocationStack */
    context->sp = frame;
    return context;
}

void khdays_context_switch(KhdaysContext *to)
{
    KhdaysContext *from = current;
    if (to == from) {
        return;
    }
    current = to;
    khdays_context_jump(&from->sp, to->sp);
}

void khdays_context_delete(KhdaysContext *context)
{
    if (context->stack != NULL) {
        context->release(context->stack, context->size);
    }
    free(context);
}

void *khdays_context_stopped_at(const KhdaysContext *context)
{
    (void)context;
    return NULL; /* diag.c walks Windows' threads itself */
}
