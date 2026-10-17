/* Cooperative execution contexts: how the port switches between the game's threads (hal/sdk_thread.c).
 * Each context has its own stack and runs until it switches to another; nothing interrupts it. On
 * Windows the switch is the port's own (context_win.c, context_x64.asm). */
#ifndef KHDAYS_HOST_CONTEXT_H
#define KHDAYS_HOST_CONTEXT_H

#include <stddef.h>

typedef struct KhdaysContext KhdaysContext;

/* Sets where the stacks of contexts created from now on come from, and how they are given back. The
 * runtime uses the game's memory, so the game's locals can be reached by reference
 * (runtime/core/memory.c). NULL means the host's own memory. */
void khdays_context_stack_source(void *(*allocate)(size_t size), void (*release)(void *stack, size_t size));

/* Makes the calling thread a context (once, before creating any other).
 * Returns NULL if it cannot. */
KhdaysContext *khdays_context_init(void);

/* The context running now. */
KhdaysContext *khdays_context_current(void);

/* Creates a context that calls `entry(argument)` the first time it is
 * switched to; `entry` must never return. Returns NULL if there is no memory. */
KhdaysContext *khdays_context_create(void (*entry)(void *argument), void *argument);

/* Runs `to`, and returns when something switches back to this context. */
void khdays_context_switch(KhdaysContext *to);

/* Where a context that is not running stopped: its stack pointer, the switch's frame there
 * (context_arm64.S); NULL where the system's does not say. For the stall report. */
void *khdays_context_stopped_at(const KhdaysContext *context);

/* Frees a context that is not running. */
void khdays_context_delete(KhdaysContext *context);

#endif
