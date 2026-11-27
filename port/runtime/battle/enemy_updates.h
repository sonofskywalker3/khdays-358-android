/* The enemies' updates (Ov107_ProcessObjectTick, prepare.py's join): the DS's game saves its time by updating a far
 * enemy (past 10, 20 or 30 units from the player), one behind the view or one marked unseen every second, third or
 * fourth frame only, the time it missed given to it at once (+0x30c the frames it waited) -- such an enemy moved by
 * jumps, which the PC's smoothed pictures showed as stutters. The port updates every enemy every frame, but on a
 * weaker card on Android (KHDAYS_HOST_ENEMIES_DS: a Mali, an Adreno below 700), where it skips them as the DS does:
 * updating them all cost too many pictures a second there. A recording of the input keeps the way it was made
 * with (replay.h); KHDAYS_DS_ENEMY_UPDATES=1 or 0 says which way anywhere. */
#ifndef KHDAYS_ENEMY_UPDATES_H
#define KHDAYS_ENEMY_UPDATES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the far enemies' updates are skipped as the DS's game skips them: on a weaker card on Android, as a
 * recording played back was made, or as KHDAYS_DS_ENEMY_UPDATES says. Decided once a session. */
int khdays_enemy_far_updates(void);

#ifdef __cplusplus
}
#endif

#endif
