/* Gameplay mods' scripts: the mods in mods/ with a mod.ini, each run in its own Lua state on the
 * game's thread, at the frame point and at the game's own events (docs/SCRIPTING.md). With no
 * scripted mod present, nothing of this is made and nothing runs. */
#ifndef KHDAYS_SCRIPTS_H
#define KHDAYS_SCRIPTS_H

/* The frame point, after the game's logic of the frame (main's loop, prepare.py's HOOKS). The first
 * call finds and loads the mods. */
void khdays_scripts_frame(void);

/* Whether a scripted mod is present (loaded, off, stopped or refused). */
int khdays_scripts_any(void);

/* Finds and loads the mods in `mods`, their data and log under `user` (the frame point does it once,
 * with the game's folders; the tests with theirs). */
void khdays_scripts_start_in(const char *mods, const char *user);

/* Closes every mod's state and forgets them. */
void khdays_scripts_stop(void);

/* The game's routines the build joins (prepare.py's HOOKS). */
void khdays_scripts_field_begins(void);                               /* Ov002_ConstructGameplayScene */
void khdays_scripts_mission_start(void);                              /* Ov002_BeginMissionRun: a run starts */
void khdays_scripts_mission_end(int mission, int rank, int complete); /* Ov005_CommitMissionResults */
void khdays_scripts_hp(void *entity, int before);                     /* Ov022_ActorSetHp, at its end */
void khdays_scripts_saved(void);                                      /* the save written (runtime/core/arm7.c) */

#endif
