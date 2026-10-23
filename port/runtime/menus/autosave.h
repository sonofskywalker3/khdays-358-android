/* The autosave: after a cleared mission, the first time a camp's menu waits for the
 * player, the camp's own save writes the file the [save] autosave setting names. */
#ifndef KHDAYS_AUTOSAVE_H
#define KHDAYS_AUTOSAVE_H

/* The [save] autosave setting (host/app/profile.h KHDAYS_AUTOSAVE_*: 0 off, 1 file 99, 2 the file
 * being played), from the window's thread. */
void khdays_autosave_set_mode(int mode);

/* The results of a mission are recorded (Ov005_CommitMissionResults, as it starts): an autosave is
 * owed when the routine records the mission cleared. */
void khdays_autosave_mission_end(void);

/* Whether the results being recorded record the mission cleared, by Ov005_CommitMissionResults' own
 * branches: the story's (reward mode 255) with flag 0x2086, mode 8 ranked 0-3, the others ranked 0-2;
 * never after a give-up (flag 0x2087); a configuration that only marks the mission seen records
 * nothing. */
int khdays_autosave_results_cleared(void);

/* At Pad_Sample (runtime/input/input.c), before the save list maps the game's slots: the owed
 * autosave written once a camp's menu waits for the player. */
void khdays_autosave_frame(void);

/* What the player is told, for the HUD's notice (ui/autosave_notice.h): the oldest autosave not told yet,
 * `*file` its file (0 on) and `*ok` whether it was written. 0 when there is none. */
int khdays_autosave_take_notice(int *file, int *ok);

#endif
