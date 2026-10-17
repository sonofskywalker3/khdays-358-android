/* A guard at the DS's I/O addresses. prepare.py (with registers.py) rewrites every register access of
 * the game's C to reach the port, so the registers' DS addresses, in the window and out of it, stay
 * inaccessible: an access there is one the rewrite missed. The game stops, naming the register and
 * the instruction (exit code 4). Any other DS address outside the window stops it too (exit code 6):
 * an access of the port's side not routed through KHDAYS_W. */
#include "../../hal/hal.h"
#include "io.h"
#include "runtime.h"
#include "session_log.h"
#include "window.h"

#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#endif

static int is_io(u32 address)
{
    return (address >= KHDAYS_IO_BASE && address < KHDAYS_IO_BASE + KHDAYS_IO_SIZE) ||
           (address >= KHDAYS_IO2_BASE && address < KHDAYS_IO2_BASE + KHDAYS_IO2_SIZE);
}

/* The DS's names for its registers (GBATEK), for the message. */
static const char *register_name(u32 address)
{
    static const struct {
        u32 from, to;
        const char *name;
    } names[] = {
        {0x04000000, 0x04000004, "DISPCNT"},
        {0x04000004, 0x04000006, "DISPSTAT"},
        {0x04000006, 0x04000008, "VCOUNT"},
        {0x04000008, 0x04000010, "BGxCNT"},
        {0x04000010, 0x04000020, "BGxHOFS/VOFS"},
        {0x04000020, 0x04000040, "BG2/3 affine"},
        {0x04000040, 0x0400004c, "the windows"},
        {0x0400004c, 0x04000050, "MOSAIC"},
        {0x04000050, 0x04000056, "the blending"},
        {0x04000060, 0x04000062, "DISP3DCNT"},
        {0x04000064, 0x04000068, "DISPCAPCNT"},
        {0x04000068, 0x0400006c, "DISP_MMEM_FIFO"},
        {0x0400006c, 0x0400006e, "MASTER_BRIGHT"},
        {0x040000b0, 0x040000e0, "DMA0-3"},
        {0x040000e0, 0x040000f0, "DMA fill"},
        {0x04000100, 0x04000110, "TMxCNT"},
        {0x04000130, 0x04000132, "KEYINPUT"},
        {0x04000132, 0x04000134, "KEYCNT"},
        {0x04000180, 0x04000184, "IPCSYNC"},
        {0x04000184, 0x04000188, "IPCFIFOCNT"},
        {0x04000188, 0x0400018c, "IPCFIFOSEND"},
        {0x040001a0, 0x040001a4, "AUXSPICNT/DATA"},
        {0x040001a4, 0x040001a8, "ROMCTRL"},
        {0x040001a8, 0x040001b0, "the card's command"},
        {0x04000204, 0x04000206, "EXMEMCNT"},
        {0x04000208, 0x0400020c, "IME"},
        {0x04000210, 0x04000214, "IE"},
        {0x04000214, 0x04000218, "IF"},
        {0x04000240, 0x0400024a, "VRAMCNT/WRAMCNT"},
        {0x04000280, 0x040002c0, "the math unit"},
        {0x04000300, 0x04000302, "POSTFLG"},
        {0x04000304, 0x04000306, "POWCNT1"},
        {0x04000320, 0x040003c0, "the 3D's settings"},
        {0x04000400, 0x04000440, "GXFIFO"},
        {0x04000440, 0x04000600, "the geometry's command ports"},
        {0x04000600, 0x04000604, "GXSTAT"},
        {0x04000604, 0x04000620, "the geometry's counts"},
        {0x04000620, 0x040006a4, "the geometry's results"},
        {0x04001000, 0x04001070, "engine B's 2D"},
        {0x04100000, 0x04100004, "IPCFIFORECV"},
        {0x04100010, 0x04100014, "the card's data port"},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (address >= names[i].from && address < names[i].to) {
            return names[i].name;
        }
    }
    return "not named";
}

/* An address of the DS's own ranges (memory.c's map), outside the window. */
static int at_ds_address(const unsigned char *host)
{
    return khdays_window != NULL && (size_t)host >= 0x01ff8000u && (size_t)host < 0x0b000000u &&
           !(host >= khdays_window && (size_t)(host - khdays_window) <= 0xffffffffu);
}


#ifdef _WIN32
#define khdays_io_guard_exit(code) ExitProcess(code)
#else
#define khdays_io_guard_exit(code) _exit(code)
#endif

/* Whether a fault at `host` is the guard's: a register's DS address in the window, or a DS address
 * outside it. Then the game stops, named; otherwise it returns. */
static void judge(const unsigned char *host, const void *site, const char *access)
{
    /* in the window, judged by the DS address alone: the host address's low bits may look like a
     * register's anywhere in the heaps */
    const int in_window = khdays_window != NULL && host >= khdays_window && (size_t)(host - khdays_window) <= 0xffffffffu;
    const u32 address = in_window ? (u32)(host - khdays_window) : (u32)(size_t)host;
    const char *function;
    if (!is_io(address) && (in_window || !at_ds_address(host))) {
        return;
    }
    function = khdays_diag_function_at(site); /* only for a stop: the names are slow */
    if (!is_io(address)) {
        fprintf(stderr,
                "khdays-native: %s (%p) %s 0x%08x at its DS address, outside the game's memory (at %p): "
                "stopped\n",
                function, site, access, address, (void *)khdays_window);
        khdays_log("stop: %s (%p) %s 0x%08x at its DS address, outside the game's memory (at %p)", function, site,
                   access, address, (void *)khdays_window);
        khdays_log_close();
        fflush(stderr);
        khdays_io_guard_exit(6);
    }
    fprintf(stderr,
            "khdays-native: %s (%p) %s the register at 0x%08x (%s), which the port does not reach: "
            "stopped\n",
            function, site, access, address, register_name(address));
    khdays_log("stop: %s (%p) %s the register at 0x%08x (%s), which the port does not reach", function, site, access,
               address, register_name(address));
    khdays_log_close();
    fflush(stderr);
    khdays_io_guard_exit(4);
}

#ifdef _WIN32
static LONG CALLBACK io_guard(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    const CONTEXT *context = info->ContextRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->ExceptionInformation[0] == 8) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    judge((const unsigned char *)record->ExceptionInformation[1], (const void *)context->Rip,
          record->ExceptionInformation[0] == 1 ? "writes" : "reads");
    return EXCEPTION_CONTINUE_SEARCH;
}

void khdays_io_guard_init(void)
{
    AddVectoredExceptionHandler(1, io_guard);
}
#else

/* The fault handler before the guard's (diag.c's crash report, or the system's): a fault the guard
 * does not take goes on to it. */
static struct sigaction previous;

static void io_guard(int signal_number, siginfo_t *info, void *context_pointer)
{
    const ucontext_t *context = (const ucontext_t *)context_pointer;
#if defined(__aarch64__)
    const void *site = (const void *)context->uc_mcontext.pc;
    /* ESR's WnR bit (in the fault's record, when the kernel gives it) says a write; else "reaches" */
    const char *access = "reaches";
#else
    const void *site = NULL;
    const char *access = "reaches";
    (void)context;
#endif
    judge((const unsigned char *)info->si_addr, site, access);
    /* not the guard's: the handler before it, or the system's on the fault taken again */
    if (previous.sa_flags & SA_SIGINFO) {
        if (previous.sa_sigaction != NULL) {
            previous.sa_sigaction(signal_number, info, context_pointer);
            return;
        }
    } else if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN && previous.sa_handler != NULL) {
        previous.sa_handler(signal_number);
        return;
    }
    sigaction(signal_number, &previous, NULL);
}

void khdays_io_guard_init(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = io_guard;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, &previous);
}
#endif
