/* memory.c on POSIX: the DS's ARM9 address space in the game's memory window (window.h), each region at
 * the window's start plus its DS address, rounded out to 64 KB; the rest inaccessible, the null range
 * among it. Main RAM and its mirrors are views of one memory file; the heaps are left to the kernel,
 * which gives a page its memory when it is first touched. The map: docs/PLATFORM.md, "Memory". */
#include "game_heap.h"
#include "io.h"
#include "runtime.h"
#include "session_log.h"
#include "window.h"
#include "../../host/app/context.h"
#include "../../sound/snd7_native.h"
#include "khdays_data_area.h" /* the window's areas above the DS's ranges (build/port/gen-*) */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define MAIN_RAM_SIZE 0x400000u
#define DTCM_BASE 0x027e0000u
#define GRANULE 0x10000u
#define ARM7_WRAM_BASE 0x037f8000u
#define ARM7_WRAM_END 0x03810000u

/* The stacks area: 256 MB, each stack after an inaccessible granule. */
#define STACKS_BASE 0x0c000000u
#define STACKS_SIZE 0x10000000u
#define WINDOW_SIZE ((size_t)KHDAYS_HEAPS_END)

_Static_assert(STACKS_BASE + STACKS_SIZE <= KHDAYS_HEAPS_BASE, "the stacks area runs into the game's heaps");

/* The host address of a DS address (window.h's W). */
static unsigned char *at(unsigned address)
{
    return khdays_window + address;
}

static int fail(const char *what, unsigned address)
{
    fprintf(stderr, "memory map: %s at 0x%08x (host %p) failed (%s)\n", what, address, (void *)at(address),
            strerror(errno));
    khdays_log("memory map: %s at 0x%08x (host %p) failed (%s)", what, address, (void *)at(address),
               strerror(errno));
    return 0;
}

static int plain(const char *what, unsigned address, unsigned size)
{
    const unsigned base = address & ~(GRANULE - 1);
    const unsigned end = (address + size + GRANULE - 1) & ~(GRANULE - 1);
    if (mmap(at(base), end - base, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) !=
        at(base)) {
        return fail(what, base);
    }
    return 1;
}

static int view(int file, const char *what, unsigned address, unsigned offset, unsigned size)
{
    if (mmap(at(address), size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, file, (off_t)offset) !=
        at(address)) {
        return fail(what, address);
    }
    return 1;
}

/* Holds the DS's own addresses outside the window, inaccessible, so that an access there stops the
 * game (io_guard.c) instead of reaching whatever the process has there. Best effort. */
static void keep_free(unsigned start, unsigned end)
{
    for (unsigned a = start; a < end; a += GRANULE) {
        void *got = mmap((void *)(size_t)a, GRANULE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE |
                                                                    MAP_FIXED_NOREPLACE,
                         -1, 0);
        if (got != MAP_FAILED && got != (void *)(size_t)a) {
            munmap(got, GRANULE); /* a kernel without MAP_FIXED_NOREPLACE placed it elsewhere */
        }
    }
}

/* The registers' own memory (io_regs.c, IO16/IO32, the C's plain registers), in the window at
 * KHDAYS_IO_VIEW. Their DS addresses stay inaccessible (io_guard.c). */
u8 *khdays_io_host;
u8 *khdays_io2_host;

/* The GBA slot, empty: it reads FFFFh, as on the DSi (GBATEK). ROM and SRAM get one read-only granule
 * each. */
static int empty_gba_slot(void)
{
    static const unsigned slots[2] = {0x08000000u, 0x0a000000u};
    for (int i = 0; i < 2; ++i) {
        if (!plain("GBA slot", slots[i], GRANULE)) {
            return 0;
        }
        memset(at(slots[i]), 0xff, GRANULE);
        mprotect(at(slots[i]), GRANULE, PROT_READ);
    }
    return 1;
}

/* The contexts' stacks (host/app/context.h), from the stacks area; a stack given back is used again
 * for one of its size. */
static unsigned stacks_next = STACKS_BASE;
static struct {
    unsigned address;
    size_t size;
} *returned;
static size_t returned_count, returned_room;

static void *window_stack(size_t size)
{
    const unsigned rounded = (unsigned)((size + GRANULE - 1) & ~(size_t)(GRANULE - 1));
    unsigned address;
    for (size_t i = 0; i < returned_count; ++i) {
        if (returned[i].size == rounded) {
            address = returned[i].address;
            returned[i] = returned[--returned_count];
            return at(address);
        }
    }
    if ((size_t)stacks_next + GRANULE + rounded > (size_t)STACKS_BASE + STACKS_SIZE) {
        fprintf(stderr, "memory map: the window's stacks area (0x%x bytes from 0x%08x) is full\n", STACKS_SIZE,
                STACKS_BASE);
        return NULL;
    }
    address = stacks_next + GRANULE; /* the granule under it stays inaccessible */
    if (!plain("a context's stack", address, rounded)) {
        return NULL;
    }
    stacks_next = address + rounded;
    return at(address);
}

static void window_stack_release(void *stack, size_t size)
{
    if (returned_count == returned_room) {
        const size_t room = returned_room ? returned_room * 2 : 16;
        void *grown = realloc(returned, room * sizeof(*returned));
        if (grown == NULL) {
            return; /* kept, never used again */
        }
        returned = grown;
        returned_room = room;
    }
    returned[returned_count].address = (unsigned)((unsigned char *)stack - khdays_window);
    returned[returned_count].size = (size + GRANULE - 1) & ~(size_t)(GRANULE - 1);
    ++returned_count;
}

/* The game's heaps: reserved without memory; the kernel gives a page its memory on first touch. */
unsigned long long khdays_heaps_committed(void)
{
    /* the kernel does not say per range without a walk of /proc; the report shows 0 */
    return 0;
}

void khdays_heaps_reset(void)
{
    /* given back: the next touch reads zeros, as a decommitted range recommitted does on Windows */
    madvise(at(KHDAYS_HEAPS_BASE), KHDAYS_HEAPS_END - KHDAYS_HEAPS_BASE, MADV_DONTNEED);
}

static int reserve_heaps(void)
{
    if (mmap(at(KHDAYS_HEAPS_BASE), KHDAYS_HEAPS_END - KHDAYS_HEAPS_BASE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) != at(KHDAYS_HEAPS_BASE)) {
        return fail("the game's heaps", KHDAYS_HEAPS_BASE);
    }
    return 1;
}

/* The window: the DS's own addresses held first, then the window reserved inaccessible as one range. */
static int reserve_window(void)
{
    void *window;
    keep_free(0x01ff0000, 0x0b010000);
    window = mmap(NULL, WINDOW_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (window == MAP_FAILED) {
        fprintf(stderr, "memory map: no room for the game's memory (0x%zx bytes, %s)\n", WINDOW_SIZE,
                strerror(errno));
        return 0;
    }
    khdays_window = (unsigned char *)window;
    khdays_snd7_native_set_ds_base((uintptr_t)khdays_window);
    khdays_context_stack_source(window_stack, window_stack_release);
    return 1;
}

/* Main RAM's memory, shared by its views: a memory file. */
static int main_ram_file(void)
{
#if defined(SYS_memfd_create)
    const int file = (int)syscall(SYS_memfd_create, "khdays-main-ram", 0);
#else
    const int file = -1;
#endif
    if (file < 0) {
        fprintf(stderr, "memory map: no memory file for main RAM (%s)\n", strerror(errno));
        return -1;
    }
    if (ftruncate(file, MAIN_RAM_SIZE) != 0) {
        fprintf(stderr, "memory map: main RAM's memory file cannot be sized (%s)\n", strerror(errno));
        close(file);
        return -1;
    }
    return file;
}

int khdays_memory_map(void)
{
    int ram;
    if (!reserve_window()) {
        return 0;
    }
    ram = main_ram_file();
    if (ram < 0) {
        return 0;
    }
    /* Main RAM and its mirrors; the second leaves the DTCM's granule out. The two upper mirrors hold the
     * headers' heaps instead, unless the heaps are made as on the DS. */
    if (!view(ram, "main RAM", 0x02000000, 0, MAIN_RAM_SIZE) ||
        !view(ram, "main RAM mirror", 0x02400000, 0, DTCM_BASE - 0x02400000) ||
        !view(ram, "main RAM mirror (shared area)", DTCM_BASE + GRANULE, DTCM_BASE + GRANULE - 0x02400000,
              0x02800000 - (DTCM_BASE + GRANULE))) {
        return 0;
    }
    if (khdays_game_heap_ds_memory()) {
        if (!view(ram, "main RAM mirror", 0x02800000, 0, MAIN_RAM_SIZE) ||
            !view(ram, "main RAM mirror", 0x02c00000, 0, MAIN_RAM_SIZE)) {
            return 0;
        }
    } else if (!plain("the headers' heaps", KHDAYS_HEADERS_BASE, 0x03000000u - KHDAYS_HEADERS_BASE)) {
        return 0;
    }
    /* The I/O registers' and the IPC/card ports' DS ranges stay inaccessible: the port reaches them. */
    if (!(plain("ITCM", 0x01ff8000, 0x8000) && plain("DTCM", DTCM_BASE, 0x4000) &&
          plain("shared WRAM", 0x03000000, 0x8000) &&
          plain("ARM7 WRAM", ARM7_WRAM_BASE, ARM7_WRAM_END - ARM7_WRAM_BASE) && plain("palettes", 0x05000000, 0x800) &&
          plain("VRAM", 0x06000000, 0x008a4000) && plain("OAM", 0x07000000, 0x800) && empty_gba_slot())) {
        return 0;
    }
    /* The rewritten game's areas above the DS's ranges (port/tools/references/build.py): the register
     * model's memory and the data area. */
    if (!(plain("the register model's memory", KHDAYS_IO_VIEW, KHDAYS_IO_SIZE + KHDAYS_IO2_SIZE) &&
          (KHDAYS_DATA_AREA_END == KHDAYS_DATA_AREA ||
           plain("the data area", KHDAYS_DATA_AREA, KHDAYS_DATA_AREA_END - KHDAYS_DATA_AREA)))) {
        return 0;
    }
    if (!reserve_heaps()) {
        return 0;
    }
    khdays_io_host = at(KHDAYS_IO_VIEW);
    khdays_io2_host = at(KHDAYS_IO2_VIEW);
    return 1;
}
