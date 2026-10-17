/* The native hardware layer: what stands in for the DS's hardware below
 * NitroSDK. */
#ifndef KHDAYS_HAL_H
#define KHDAYS_HAL_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char s8;
typedef short s16;
typedef int s32;
typedef long long s64;
/* A native address as an integer (an instruction's, for the diagnostics):
 * the C runtime's uintptr_t, which the game's side does without. */
#if defined(_WIN64) || defined(__LP64__)
typedef unsigned long long khdays_uptr;
#else
typedef unsigned int khdays_uptr;
#endif
typedef s32 fx32;

#ifdef __cplusplus
extern "C" {
#endif

/* Stops the process naming a routine the port has not implemented yet. The
 * HAL never guesses at behaviour: a path that reaches one of these is a gap to
 * fill from the SDK/hardware documentation, not something to paper over. */
__declspec(noreturn) void khdays_hal_unimplemented(const char *what);

#define KHDAYS_HAL_TODO(name)                                                                                          \
    void name(void)                                                                                                    \
    {                                                                                                                  \
        khdays_hal_unimplemented(#name);                                                                               \
    }

/* The emulated ARM9 CPSR: the mode (bits 0-4) and the IRQ/FIQ disable bits
 * (0x80/0x40). Nothing else of it has a native meaning. */
extern u32 khdays_cpsr;

/* Where the game would halt the CPU or spin until an interrupt: the runtime
 * delivers what is pending (VBlank, PXI replies) and returns once at least
 * one interrupt has been delivered. */
void khdays_runtime_wait(void);

/* Interrupts were just unmasked: take what is pending (events.c). */
void khdays_irq_poll(void);

#ifdef __cplusplus
}
#endif

#endif
