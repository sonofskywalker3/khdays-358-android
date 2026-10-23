/* The native sound driver's entry (snd7_native.h): SND_Init, the driver's
 * thread, and the interface the bridge calls (port/sound/snd7.h). */
#include "snd7_internal.h"
#include "snd7_native.h"
#include "snd7_os.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

KhdaysSoundHw *snd7_channels;
const KhdaysSnd7Host *snd7_host;
uintptr_t snd7_ds_base;
const uint8_t *snd7_segment;
uint64_t snd7_now;

static uint32_t random_state; /* 0x03807404, initialised data */

void khdays_snd7_native_set_ds_base(uintptr_t base)
{
    snd7_ds_base = base;
}

void khdays_snd7_native_set_segment(const uint8_t *segment)
{
    snd7_segment = segment;
}

/* 0x037feec4 */
uint16_t snd7_random(void)
{
    random_state = random_state * 0x0019660du + 0x3c6ef35fu;
    return (uint16_t)(random_state >> 16);
}

void snd7_random_reset(void)
{
    random_state = snd7_segment_u32(0x03807404u);
}

__declspec(noreturn) void snd7_stop(const char *format, ...)
{
    va_list args;
    fprintf(stderr, "khdays-native: native sound driver: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
    exit(15);
}

/* The driver thread's periodic alarm (0x03807820) and its handler
 * (0x037fefec): message 1, every 0xaa8 = 2728 ticks (5.21 ms). */
static Snd7OsAlarm periodic;

static void periodic_fired(uint32_t arg)
{
    (void)arg;
    snd7_os_send_message(1);
}

/* 0x037fef64 */
void snd7_interval_timer_start(void)
{
    snd7_os_set_periodic_alarm(&periodic, snd7_os_tick() + 0x10000, 0xaa8, periodic_fired, 0);
}

/* 0x037fefb4 */
void snd7_interval_timer_stop(void)
{
    snd7_os_cancel_alarm(&periodic);
}

/* 0x037ff008's loop, once per message: with message 1 the periodic work
 * (the sequencer's and the channels' steps). */
void snd7_thread_loop(uint32_t message)
{
    const int is_periodic = message == 1;
    snd7_channel_sync();
    snd7_command_process();
    snd7_seq_main(is_periodic);
    snd7_channel_main(is_periodic);
    snd7_update_shared_status();
    snd7_random();
}

/* 0x037ff008's start (its periodic alarm set as 0x037fef64 sets it). */
static void thread_start(void)
{
    snd7_os_create_alarm(&periodic);
    snd7_channel_init();
    snd7_seq_init();
    snd7_alarm_init();
    snd7_hw_enable();
    snd7_hw_set_output_selector(0, 0, 0, 0);
    snd7_hw_set_master_volume(0x7f);
    snd7_interval_timer_start();
}

/* 0x037feef8: SND_Init -- the FIFO side (0x03801ed8), then the thread,
 * which runs at once until it waits for its first message. */
static int start(KhdaysSoundHw *hw, const KhdaysSnd7Host *bridge, uint64_t cycle)
{
    if (snd7_segment == NULL) {
        fprintf(stderr, "sound: the native driver has no ARM7 segment to read its tables from\n");
        return 0;
    }
    snd7_channels = hw;
    snd7_host = bridge;
    snd7_now = cycle;
    snd7_os_reset();
    snd7_hw_reset();
    snd7_random_reset();
    snd7_seq_reset();
    snd7_command_init();
    thread_start();
    return 1;
}

/* A word on the sound's tag: the FIFO callback, then the thread takes what
 * it was sent. */
static void word(uint32_t data, uint32_t err)
{
    (void)err;
    snd7_command_receive(data);
    snd7_os_after_interrupt();
}

static void advance(uint64_t cycle)
{
    snd7_os_advance(cycle);
}

static uint64_t next_event(void)
{
    return snd7_os_next_event();
}

const KhdaysSnd7Driver khdays_snd7_native = {"native",
                                             start,
                                             word,
                                             advance,
                                             next_event,
                                             snd7_seq_channel_player,
                                             khdays_snd7_native_player_record,
                                             snd7_seq_player_stops};
