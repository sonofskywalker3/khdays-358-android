/* Each mod's own Lua state, with what a mod may use and nothing else: no files but its own folder's
 * scripts and data, no programs, no native code, no network; its memory capped and its time per frame
 * counted; every call into it protected, so an error stops that mod only. */
#ifndef KHDAYS_SCRIPTS_SANDBOX_H
#define KHDAYS_SCRIPTS_SANDBOX_H

#include "mod.h"

#include <stdint.h>

#define KHDAYS_MOD_MEMORY ((size_t)64 << 20) /* bytes a mod's state may hold */

/* The allowances, in ns: a mod's scripts per game frame, and its main script when it is loaded. */
extern uint64_t khdays_sandbox_frame_allowance;
extern uint64_t khdays_sandbox_load_allowance;

/* Makes the mod's state with its libraries. Returns 0 (the mod refused, its reason set) on failure. */
int khdays_sandbox_open(KhdaysMod *mod);

void khdays_sandbox_close(KhdaysMod *mod);

/* Loads the mod's main script and runs it. Returns 1, or 0 with the mod stopped or paused. */
int khdays_sandbox_run_main(KhdaysMod *mod);

/* Calls the function under `nargs` arguments on the mod's stack, protected, within its allowance for
 * this frame. Returns 1 with `nresults` results left, or 0 with nothing left: the mod is stopped
 * (an error) or paused (over its allowance), and that is logged. */
int khdays_sandbox_call(KhdaysMod *mod, int nargs, int nresults);

/* The mod a state (or one of its coroutines) belongs to. */
KhdaysMod *khdays_sandbox_mod(struct lua_State *L);

/* The script file and line running at `level` of the state's calls (1: the function calling C). */
void khdays_sandbox_where(struct lua_State *L, int level, char *file, int size, int *line);

/* Whether a path a mod gives (for its data) stays inside its folder: no "..", no absolute path, no
 * drive, no backslash, no empty part. */
int khdays_sandbox_path_ok(const char *path);

#endif
