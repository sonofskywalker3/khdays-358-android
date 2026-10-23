/* Time, music, the room's seed and the mods' shares, the game's side (api_time.c has the mods' side).
 * The tests give their own. */
#ifndef KHDAYS_SCRIPTS_TIME_LINK_H
#define KHDAYS_SCRIPTS_TIME_LINK_H

/* The VBlanks counted since the game began (60 a second, the host's clock): the mods' timers' time. */
unsigned khdays_link_vblanks(void);

/* The music a mod plays, on the game's music player as the game's own (SoundMgr_SwitchBgm): `track` a
 * sequence of the sound archive, its number by its name ("TwilightR_F" or "seq/TwilightR_F"; -1 none).
 * While it plays, the game's own requests to the sound manager are kept back (prepare.py's joins call
 * khdays_link_music_request: 1 when it keeps one); stopping it makes the last of them, else plays the
 * track it took the place of; it stops by itself as the scene changes (khdays_link_music_frame). */
int khdays_link_music_track(const char *name);
const char *khdays_link_music_play(int track, int fade);
void khdays_link_music_stop(int fade);
int khdays_link_music_playing(void);
void khdays_link_music_frame(void);
int khdays_link_music_request(int kind, int arg, int value);

/* The seed of the mods' draws: in a room the room's (chosen as it opened, changed by the host as each
 * mission ends; a guest's from the host, net_session.h), else this copy's own. *in_room says which;
 * 0 on a guest whose host has not sent it yet. */
unsigned long long khdays_link_seed(int *in_room);
/* A mission ended: the host (or a copy alone) picks the next mission's seed. */
void khdays_link_next_seed(void);
/* The host: bytes the mods share, to the room's guests (any size); 0 when nobody is there. A guest: the
 * next message the host's mods shared, in a block the caller frees; 0 when none waits. */
int khdays_link_share(const void *data, unsigned size);
int khdays_link_shared(unsigned char **data, unsigned *size);

#endif
