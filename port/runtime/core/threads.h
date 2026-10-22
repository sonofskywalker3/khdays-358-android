/* The game's threads on the port's execution contexts (host/app/context.h): the port's side of the
 * SDK's OS_InitContext, OS_SaveContext and OS_LoadContext (hal/sdk_thread.c). Each OSContext that has
 * run owns one execution context, in a table that grows. */
#ifndef KHDAYS_THREADS_H
#define KHDAYS_THREADS_H

/* The runtime starts the game on a context so the scheduler can switch away
 * from it. */
void khdays_threads_init(void);

/* The game's reset (start.c): every thread's execution context deleted --
 * but the running one, which is the reset's own, not a thread's. */
void khdays_threads_reset(void);

/* The game's threads' contexts (their OSContext references) and what runs each, for the stall report:
 * at most `capacity`; their number. */
struct KhdaysContext;
int khdays_threads_list(unsigned int *contexts, struct KhdaysContext **running, int capacity);

/* OS_InitContext: the context at `context` (re)starts -- its execution
 * context made anew, running the function its image names (pc+4, r0, lr:
 * the OSContext's own words) the first time it is switched to. */
void khdays_thread_start(unsigned int context);

/* OS_SaveContext: a context never started is the thread already running (the
 * launcher): the running execution context becomes its. */
void khdays_thread_adopt(unsigned int context);

/* OS_LoadContext: the thread of `context` runs; this returns when the
 * calling thread is switched back to. */
void khdays_thread_switch(unsigned int context);

/* KHDAYS_TRACE_THREADS: one line for an init or a switch (the context's
 * entry, stack and cpsr), with the SDK's thread list. */
void khdays_thread_trace(const char *what, unsigned int context);
int khdays_thread_tracing(void);

#endif
