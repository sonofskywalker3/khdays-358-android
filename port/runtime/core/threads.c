/* The game's threads on the port's execution contexts (threads.h): the SDK's scheduler runs unchanged;
 * OS_SaveContext returns 0 and records the context, OS_LoadContext switches, and a thread scheduled
 * again resumes inside that call. Each thread runs on its context's own stack (docs/PLATFORM.md). */
#include "threads.h"
#include "window.h"
#include "../../hal/hal.h"
#include "../../hal/os_context.h"
#include "../../host/app/context.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define CONTEXT_R0 ((u32)offsetof(OSContext, r))
#define CONTEXT_LR ((u32)offsetof(OSContext, lr))
#define CONTEXT_PC_PLUS4 ((u32)offsetof(OSContext, pc_plus4))

static u32 word(u32 reference)
{
    return *(volatile u32 *)KHDAYS_W(reference);
}

/* KHDAYS_TRACE_THREADS: each thread creation and switch, with the SDK's thread list
 * (OSi_ThreadInfo.list at 0x02044338; OSThread state +0x64, next +0x68, priority +0x70). */
static int trace_threads;

int khdays_thread_tracing(void)
{
    return trace_threads;
}

void khdays_thread_trace(const char *what, unsigned int context)
{
    const OSContext *image = (const OSContext *)KHDAYS_W(context);
    u32 t = word(0x02044338u);
    fprintf(stderr, "thread: %s %08x (entry %08x, stack %08x, cpsr %02x)\n  list:", what, context, image->pc_plus4 - 4,
            image->sp_svc, image->cpsr);
    for (int n = 0; t != 0 && n < 32; ++n) {
        fprintf(stderr, " %08x(s%u,p%u)", t, word(t + 0x64), word(t + 0x70));
        t = word(t + 0x68);
    }
    fprintf(stderr, "\n");
}

typedef struct Slot {
    u32 context;            /* the OSContext's reference, 0 for a free entry */
    KhdaysContext *running; /* what runs it */
} Slot;
static Slot *slots;
static int nslots, room;

/* A context replaced while it was running (a thread re-initialising its own
 * context to run its destructor), deleted once another one runs. */
static KhdaysContext *retired;

static void delete_retired(void)
{
    if (retired != NULL && retired != khdays_context_current()) {
        khdays_context_delete(retired);
        retired = NULL;
    }
}

static int slot_of(u32 context)
{
    for (int i = 0; i < nslots; ++i) {
        if (slots[i].context == context) {
            return i;
        }
    }
    return -1;
}

/* A free entry, the table grown when it has none. */
static int free_slot(void)
{
    for (int i = 0; i < nslots; ++i) {
        if (slots[i].context == 0) {
            return i;
        }
    }
    if (nslots == room) {
        const int grown_room = room ? room * 2 : 32;
        Slot *grown = (Slot *)realloc(slots, (size_t)grown_room * sizeof(*grown));
        if (grown == NULL) {
            khdays_hal_unimplemented("no memory for another thread context");
        }
        slots = grown;
        room = grown_room;
    }
    slots[nslots].context = 0;
    slots[nslots].running = NULL;
    return nslots++;
}

/* The thread's first run: enter its function with r0 as the argument, and on
 * return go where lr points (the SDK sets it to the thread-exit routine). */
static void thread_entry(void *parameter)
{
    const u32 context = (u32)(size_t)parameter;
    const u32 entry = word(context + CONTEXT_PC_PLUS4) - 4;
    const u32 argument = word(context + CONTEXT_R0);
    const u32 exit_hook = word(context + CONTEXT_LR);
    delete_retired();
    ((void (*)(u32))KHDAYS_FN(entry))(argument);
    if (exit_hook != 0) {
        ((void (*)(void))KHDAYS_FN(exit_hook))();
    }
    khdays_hal_unimplemented("a thread function returned with no exit hook");
}

void khdays_thread_start(unsigned int context)
{
    int slot = slot_of(context);
    if (slot < 0) {
        slot = free_slot();
    } else if (slots[slot].running == khdays_context_current()) {
        retired = slots[slot].running;
    } else {
        khdays_context_delete(slots[slot].running);
    }
    slots[slot].context = context;
    slots[slot].running = khdays_context_create(thread_entry, (void *)(size_t)context);
    if (slots[slot].running == NULL) {
        khdays_hal_unimplemented("no memory for a thread's context");
    }
}

void khdays_thread_adopt(unsigned int context)
{
    if (slot_of(context) < 0) {
        const int slot = free_slot();
        slots[slot].context = context;
        slots[slot].running = khdays_context_current();
    }
}

void khdays_thread_switch(unsigned int context)
{
    const int slot = slot_of(context);
    if (slot < 0) {
        khdays_hal_unimplemented("OS_LoadContext of a context with no thread");
    }
    if (slots[slot].running != khdays_context_current()) {
        khdays_context_switch(slots[slot].running);
        delete_retired();
    }
}

void khdays_threads_reset(void)
{
    KhdaysContext *const current = khdays_context_current();
    for (int i = 0; i < nslots; ++i) {
        if (slots[i].running != NULL && slots[i].running != current) {
            khdays_context_delete(slots[i].running);
        }
        slots[i].context = 0;
        slots[i].running = NULL;
    }
    nslots = 0;
    if (retired != NULL && retired != current) {
        khdays_context_delete(retired);
    }
    retired = NULL;
}

void khdays_threads_init(void)
{
    trace_threads = getenv("KHDAYS_TRACE_THREADS") != NULL;
    if (khdays_context_init() == NULL) {
        khdays_hal_unimplemented("the game's thread cannot become a context");
    }
}

int khdays_threads_list(unsigned int *contexts, KhdaysContext **running, int capacity)
{
    int n = 0;
    for (int i = 0; i < nslots && n < capacity; ++i) {
        if (slots[i].context != 0) {
            contexts[n] = slots[i].context;
            running[n] = slots[i].running;
            ++n;
        }
    }
    return n;
}
