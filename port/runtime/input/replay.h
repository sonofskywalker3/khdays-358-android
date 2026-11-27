/* The player's input written down with the session log, and played back (replay.c).
 *
 * With the session log on from the start, the input (the host's snapshot, as the game takes it at
 * Pad_Sample, and the buttons at the game's other reads) goes to saves/logs/session-<time>.input
 * whenever it changes, with the
 * clock's readings, and the save files as they were at the start are copied beside it
 * (session-<time>.saves/). KHDAYS_REPLAY=<file.input> plays such a file back instead of the player:
 * the same input at the same reads of it, the same clock. Started from those saves, the game
 * walks the player's way again as long as it decides the same from the same input. */
#ifndef KHDAYS_REPLAY_H
#define KHDAYS_REPLAY_H

#include "../../hal/hal.h"
#include "../../host/app/host.h"

/* Reads KHDAYS_REPLAY (at start). */
void khdays_replay_init(void);

/* Whether a recording is being played back. */
int khdays_replay_playing(void);

/* Game frame `frame` takes `snapshot`: written down when it changed, or, played back, replaced by
 * the recording's. `script_keys`: an input script's keys held, written down with the player's (a
 * scripted run recorded with KHDAYS_RECORD_INPUT=1, to try its playback). */
void khdays_replay_snapshot(u32 frame, KhdaysInputSnapshot *snapshot, u16 script_keys);

/* Another read of the buttons than Pad_Sample's (a movie's): `buttons` held now, written down when
 * they changed; played back, the recording's changes up to this read are taken. */
void khdays_replay_read(u16 buttons);

/* The DS buttons the playback holds now (its latest snapshot's). */
u16 khdays_replay_buttons(void);

/* The stylus the playback holds now: 0 when not touching. */
int khdays_replay_touch(int *x, int *y);

/* The clock the game reads (the ARM7's RTC): written down, or, played back, the recording's next
 * reading in place of the PC's. */
void khdays_replay_time(int *hour, int *minute, int *second);

#endif
