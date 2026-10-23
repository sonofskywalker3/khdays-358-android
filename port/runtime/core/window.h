/* The game's memory as the port's side reaches it. The game keeps its pointers as 4-byte references
 * (the DS addresses they had): KHDAYS_W turns one into a host address, KHDAYS_REF a host address into
 * one, KHDAYS_FN a function reference into its native function (window.c). */
#ifndef KHDAYS_WINDOW_H
#define KHDAYS_WINDOW_H

#include <stddef.h>

typedef unsigned int KhdaysRef;

/* The host address of reference 0: where the game's memory starts. */
extern unsigned char *khdays_window;

#define KHDAYS_W(reference) ((void *)(khdays_window + (KhdaysRef)(reference)))

/* The game's heaps' region: above the DS's ranges and the stacks, below 0x80000000 (the game keeps
 * heap handles and free sizes in int). The main heap's span, then the sub heap's; a granule is
 * committed when first touched (memory.c). */
#define KHDAYS_HEAPS_BASE 0x20000000u
#define KHDAYS_MAIN_HEAP_END 0x30000000u
#define KHDAYS_HEAPS_END 0x80000000u

/* The headers' heaps: a packed archive's header is named in the game's file handles by 24 bits from
 * 0x01ff8000 (Archive_OpenSubfileByHandle), so it must lie below 0x02ff8000. They sit where the DS's
 * upper main RAM mirrors were: the sub heap's from KHDAYS_HEADERS_BASE, the main heap's from
 * KHDAYS_HEADERS_MID. */
#define KHDAYS_HEADERS_BASE 0x02800000u
#define KHDAYS_HEADERS_MID 0x02c00000u
#define KHDAYS_HEADERS_END 0x02ff8000u

/* Whether `reference` lies in memory the game reads and writes as its RAM:
 * main RAM (0x02000000-0x02400000), the headers' heaps or the heaps' region.
 * (The game's C asks khdays_game_ram_ref, window.c.) */
static inline int khdays_game_ram(KhdaysRef reference)
{
    return (reference >= 0x02000000u && reference < 0x02400000u) ||
           (reference >= KHDAYS_HEADERS_BASE && reference < KHDAYS_HEADERS_END) ||
           (reference >= KHDAYS_HEAPS_BASE && reference < KHDAYS_HEAPS_END);
}

/* How many bytes of the heaps' region the PC has given the game so far. */
unsigned long long khdays_heaps_committed(void);

/* Whether the heaps' granule holding `reference` has memory (the game touched it); `*rest` the bytes
 * from `reference` to its end. A granule without reads as 0, and a reader that asks first gives it none. */
int khdays_heaps_touched(KhdaysRef reference, unsigned *rest);

/* A host address in the window as a reference; one outside it stops the game,
 * naming the caller. */
KhdaysRef khdays_reference(const void *host);
#define KHDAYS_REF(host) khdays_reference((const void *)(host))

/* The native function of a function reference: a DS address (the decomp's
 * functions, by the loaded overlay where several share it) or one the build
 * numbered (compat/khdays_references.h); an unknown one stops the game. */
typedef void (*khdays_function_pointer)(void);
khdays_function_pointer khdays_function(KhdaysRef reference);
#define KHDAYS_FN(reference) khdays_function((KhdaysRef)(reference))
/* The same, NULL for an unknown one (the diagnostics' names, checks). */
khdays_function_pointer khdays_function_lookup(KhdaysRef reference);
/* A DS address may name another function now: an overlay was loaded over
 * another (overlays.c). */
void khdays_function_references_moved(void);

#endif
