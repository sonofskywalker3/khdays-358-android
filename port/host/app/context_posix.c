/* The contexts on POSIX for 64-bit ARM (context.h): the switch is the port's own code
 * (context_arm64.S); each context runs on a stack the port allocates, in the game's memory when the
 * runtime provides it. */
#include "context.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(__aarch64__)
#error "context_posix.c: a switch for 64-bit ARM (context_arm64.S) only"
#endif

/* A context's stack: as much as a Windows build's main thread reserves, twice over for ARM's frames. */
#define STACK_SIZE (2u << 20)
#define GUARD_SIZE 0x10000u
/* the frame khdays_context_jump saves (context_arm64.S) */
#define FRAME_SIZE 176
#define FRAME_X19 0
#define FRAME_X29 80
#define FRAME_X30 88
#define FRAME_FPCR 160

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

/* A stack in the host's own memory, with a guard below it to catch an overflow. */
static void *host_stack(size_t size)
{
    unsigned char *base =
        (unsigned char *)mmap(NULL, size + GUARD_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        return NULL;
    }
    mprotect(base, GUARD_SIZE, PROT_NONE);
    return base + GUARD_SIZE;
}

static void host_release(void *stack, size_t size)
{
    munmap((unsigned char *)stack - GUARD_SIZE, size + GUARD_SIZE);
}

void khdays_context_started(KhdaysContext *context)
{
    context->entry(context->argument);
    /* an entry must never return (context.h): stop the process at once */
    abort();
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
    unsigned char *frame;
    if (context == NULL) {
        return NULL;
    }
    context->size = STACK_SIZE;
    context->stack = (unsigned char *)(allocate_stack ? allocate_stack(context->size) : host_stack(context->size));
    context->release = allocate_stack ? release_stack : host_release;
    if (context->stack == NULL) {
        free(context);
        return NULL;
    }
    context->entry = entry;
    context->argument = argument;
    /* Build the frame the first switch to it pops (context_arm64.S): it returns into the trampoline,
     * which starts the context; x29 = 0 ends the frame chain. */
    frame = (unsigned char *)(((uintptr_t)(context->stack + context->size) & ~(uintptr_t)15) - FRAME_SIZE);
    memset(frame, 0, FRAME_SIZE);
    *(uint64_t *)(frame + FRAME_X19) = (uint64_t)(uintptr_t)context;
    *(uint64_t *)(frame + FRAME_X29) = 0;
    *(uint64_t *)(frame + FRAME_X30) = (uint64_t)(uintptr_t)khdays_context_trampoline;
    *(uint64_t *)(frame + FRAME_FPCR) = 0; /* round to nearest, no traps */
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
    return context != current ? context->sp : NULL;
}
