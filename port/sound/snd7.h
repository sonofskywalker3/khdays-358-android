/* The ARM7's sound driver as the port runs it: the part of the DS the game hands its sound to
 * (NitroSDK's protocol on the FIFO's sound tag, replies and alarms back, the shared work). The native
 * driver (snd7_*.c) implements it, the tests one of their own; the bridge (runtime/sound/
 * sound_bridge.c) owns the time and the lock and calls it through this interface (docs/SOUND.md). */
#ifndef KHDAYS_SOUND_SND7_H
#define KHDAYS_SOUND_SND7_H

#include "channels.h"

#include <stdint.h>

/* What the bridge gives a driver. */
typedef struct KhdaysSnd7Host {
    void *context;
    /* The channels run up to `cycle` (the mixer takes the samples due by
     * then); a driver calls it before each thing it does at a later time. */
    void (*run)(void *context, uint64_t cycle);
    /* A word to the ARM9 on the FIFO: tag, data, error bit. */
    void (*reply)(void *context, uint32_t tag, uint32_t data, uint32_t err);
} KhdaysSnd7Host;

typedef struct KhdaysSnd7Driver {
    const char *name;
    /* Starts the driver at `cycle`, over `hw` (the channels it drives), as
     * the ARM7 starts it at boot. Returns 0, and says why, when it cannot. */
    int (*start)(KhdaysSoundHw *hw, const KhdaysSnd7Host *host, uint64_t cycle);
    /* A word the ARM9 sent on the sound's tag, taken at the driver's time. */
    void (*word)(uint32_t data, uint32_t err);
    /* Runs the driver's alarms due up to `cycle`, the channels running
     * between them, and ends with the channels at `cycle`. */
    void (*advance)(uint64_t cycle);
    /* The cycle of its next alarm, ~0 when none. */
    uint64_t (*next_event)(void);
    /* The sequence player (0-15) whose note channel `n` plays, -1 if none:
     * a channel the game set up itself, or a note its track let go. */
    int (*channel_player)(int channel);
    /* Sequence player n's record (0x24 bytes, the ARM7's layout: +0 flags --
     * bit 0 active, 1 started, 2 paused -- +5 its volume, +6 the ARM9's
     * volume in tenths of a dB, SND_SetPlayerVolume's). */
    const uint8_t *(*player_record)(int player);
    /* How many times the ARM9 has asked sequence player n to stop (SND_StopSeq): a sequence that ended
     * by itself counts none. NULL: not known. */
    uint32_t (*player_stops)(int player);
} KhdaysSnd7Driver;

#endif
