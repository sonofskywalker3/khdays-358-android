/* Several copies of the port on one PC. Each copy takes the first free player slot and keeps it while
 * it runs: slot 1 uses `saves/` as always, slot N `saves/player<N>/` for its save, settings and logs,
 * and plays with the Nth gamepad. The Windows version is instance_win.c (one named lock per slot). */
#ifndef KHDAYS_HOST_INSTANCE_H
#define KHDAYS_HOST_INSTANCE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This copy's slot, from 1 up. It is taken on the first call, made when the
 * game starts. KHDAYS_PLAYER_SLOT can name it instead (the walks use 1). */
int khdays_host_player_slot(void);

/* The slot taken, or 0 while none is (the tools and tests never take one). */
int khdays_host_player_slot_taken(void);

/* The folder for this slot's files, without a trailing slash: "saves" or
 * "saves/player<N>" (created on the first call). "saves" while no slot is
 * taken. */
const char *khdays_host_user_folder(void);

/* The folder for this slot's save files (khdays.sav, slots/): the user folder, or on Android a folder of
 * the player's own (host/android/saves_folder.h), decided at the first call (the game's first read of its
 * save). */
const char *khdays_host_saves_folder(void);

/* Whether another copy of the port is running on this machine right now. */
int khdays_host_other_copies(void);

/* The player's name as the DS's firmware holds it (at most `room` UTF-16 characters); its length.
 * KHDAYS_NAME, else the settings' `nickname`, else "Player" and four digits from this machine; from
 * player 2 on the slot's number added; printable ASCII only. Never the system's user name: it can be a
 * real name, and online anyone sees it. */
int khdays_host_player_name(const char *nickname, unsigned short *name, int room);

/* The automatic nickname: "Player" and four digits made from this machine. */
void khdays_host_automatic_name(char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
