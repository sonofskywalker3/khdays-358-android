/* Which overlay occupies which address now. Overlays share address ranges, and a call into one runs
 * whatever overlay was loaded there last. Here each overlay's code is its own function, so such a call
 * goes through its function reference (window.c), whose native function is the loaded overlay's
 * (khdays_native_at). */
#include "../../hal/hal.h"
#include "events.h"
#include "rom.h"
#include "session_log.h"
#include "window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Overlay {
    u32 start, end; /* RAM range, BSS included */
    u32 loaded;     /* load order, 0 = not loaded */
} Overlay;

static Overlay *overlays; /* by id, as many as the ROM's table has */
static int count = -1;
static u32 loads;

/* The ARM9 overlay table (header 0x50/0x54): 32-byte entries of id, RAM address, RAM size, BSS size,
 * static initializers and file id. */
static void read_table(void)
{
    const u8 *header = khdays_rom_header();
    const u32 offset = *(const u32 *)(header + 0x50);
    const u32 size = *(const u32 *)(header + 0x54);
    const u32 entries = size / 32;
    count = 0;
    overlays = entries != 0 ? (Overlay *)calloc(entries, sizeof(Overlay)) : NULL;
    if (overlays == NULL) {
        return;
    }
    for (u32 at = 0; at + 32 <= size; at += 32) {
        u32 entry[8];
        khdays_rom_read(offset + at, (u8 *)entry, sizeof(entry));
        if (entry[0] < entries) {
            overlays[entry[0]].start = entry[1];
            overlays[entry[0]].end = entry[1] + entry[2] + entry[3];
            if ((int)entry[0] >= count) {
                count = (int)entry[0] + 1;
            }
        }
    }
}

/* FS_StartOverlay ran for overlay `id`: it now holds its range, and any
 * overlay it overlaps is gone. */
void khdays_overlay_started(int id)
{
    if (count < 0) {
        read_table();
    }
    if (id < 0 || id >= count) {
        return;
    }
    for (int n = 0; n < count; ++n) {
        if (n != id && overlays[n].loaded && overlays[n].start < overlays[id].end &&
            overlays[id].start < overlays[n].end) {
            overlays[n].loaded = 0;
        }
    }
    overlays[id].loaded = ++loads;
    khdays_function_references_moved(); /* its functions' DS addresses are its now (window.c) */
    khdays_log("overlay %d started", id);
    if (getenv("KHDAYS_TRACE_OVERLAYS") != NULL) {
        /* the scene controller's current and pending ids (runtime.c) */
        const volatile u32 *scene = (const volatile u32 *)KHDAYS_W(0x0204bda8);
        fprintf(stderr, "overlay: %d started at 0x%08x (frame %u, scene %u, pending %u)\n", id, overlays[id].start,
                khdays_events_vblank_count(), scene[2], scene[3]);
    }
}

/* The game unloaded overlay `id` (FS_UnloadOverlayImage); its bytes stay until another overlay
 * overwrites them. */
void khdays_overlay_ended(int id)
{
    if (count < 0) {
        read_table();
    }
    if (id < 0 || id >= count || !overlays[id].loaded) {
        return;
    }
    overlays[id].loaded = 0;
    khdays_function_references_moved();
    khdays_log("overlay %d ended", id);
    if (getenv("KHDAYS_TRACE_OVERLAYS") != NULL) {
        fprintf(stderr, "overlay: %d ended (frame %u)\n", id, khdays_events_vblank_count());
    }
}

/* The game's reset (start.c): no overlay is loaded any more. */
void khdays_overlays_reset(void)
{
    for (int n = 0; n < count; ++n) {
        overlays[n].loaded = 0;
    }
    loads = 0;
    khdays_function_references_moved();
}

/* Whether overlay `id` is loaded now (no other started over it since). */
int khdays_overlay_loaded(int id)
{
    if (count < 0) {
        read_table();
    }
    return id >= 0 && id < count && overlays[id].loaded != 0;
}

/* The overlay loaded at `address`, or -1. */
int khdays_overlay_at(u32 address)
{
    int best = -1;
    for (int n = 0; n < count; ++n) {
        if (overlays[n].loaded && overlays[n].start <= address && address < overlays[n].end &&
            (best < 0 || overlays[n].loaded > overlays[best].loaded)) {
            best = n;
        }
    }
    return best;
}

/* The native function at a DS address (build/port/gen-x64/ds_functions.c, sorted by address): the
 * main module's, the ITCM's while its bytes are still the game's, or the loaded overlay's. NULL
 * elsewhere. */
typedef struct {
    unsigned address;
    int module;    /* -1: the main module; -2: the ITCM */
    unsigned size; /* the function's bytes (the ITCM's are compared) */
    void (*native)(void);
} KhdaysDsFunction;
extern const KhdaysDsFunction khdays_ds_functions[];
extern const unsigned khdays_ds_function_count;

/* The ITCM as the game starts: a movie copies the MobiClip decoder over it (Ov024), and an ITCM
 * address then holds the decoder's code, not the function compiled for it. */
#define ITCM_BASE 0x01ff8000u
#define ITCM_SIZE 0x8000u
static u8 itcm_image[ITCM_SIZE];
static int itcm_kept;

void khdays_itcm_keep(void)
{
    memcpy(itcm_image, KHDAYS_W(ITCM_BASE), ITCM_SIZE);
    itcm_kept = 1;
}

static int itcm_is_the_games(const KhdaysDsFunction *f)
{
    const u32 at = f->address - ITCM_BASE;
    return itcm_kept && f->size != 0 && f->address >= ITCM_BASE && at + f->size <= ITCM_SIZE &&
           memcmp(KHDAYS_W(f->address), itcm_image + at, f->size) == 0;
}

void *khdays_native_at(u32 address)
{
    unsigned lo = 0, hi = khdays_ds_function_count;
    address &= ~1u; /* a Thumb function's address carries bit 0 */
    while (lo < hi) {
        const unsigned mid = (lo + hi) / 2;
        if (khdays_ds_functions[mid].address < address) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (; lo < khdays_ds_function_count && khdays_ds_functions[lo].address == address; ++lo) {
        const KhdaysDsFunction *f = &khdays_ds_functions[lo];
        if (f->module == -1 || (f->module == -2 && itcm_is_the_games(f)) ||
            (f->module >= 0 && khdays_overlay_at(address) == f->module)) {
            return (void *)f->native;
        }
    }
    return NULL;
}
