/* The interface's version 2 (docs/SCRIPTING.md, "The interface, version 2"): what a game mode of a mod's
 * own needs, in the states of the mods that ask for api 2 only. Each part keeps what it holds for a mod
 * in the mod's KhdaysModV2 and lets it go when the mod stops. */
#ifndef KHDAYS_SCRIPTS_API_V2_H
#define KHDAYS_SCRIPTS_API_V2_H

#include "game_link.h"
#include "mod.h"

struct lua_State;

/* An enemy a mod made, while it stands. */
typedef struct KhdaysModEnemy {
    int handle;
    unsigned actor; /* the game's actor (a reference into its memory) */
    int kind;
} KhdaysModEnemy;

/* A screen or title entry a mod made, its functions in the mod's registry (LUA_NOREF without). */
typedef struct KhdaysModCallbacks {
    int handle;
    int on_choose, on_cancel, on_close, on_select;
} KhdaysModCallbacks;

/* A timer a mod set (api_time.c). */
typedef struct KhdaysModTimer KhdaysModTimer;

typedef struct KhdaysModV2 {
    KhdaysModEnemy *enemies;
    int enemy_count, enemy_room;
    int owner;   /* its number among the screens' owners (screens.h) */
    int loading; /* its main script runs: title entries are made then */
    KhdaysModCallbacks *callbacks;
    int callback_count, callback_room;
    KhdaysModTimer *timers;
    int timer_count, timer_room;
    unsigned long long draws[4]; /* its `random`'s state */
} KhdaysModV2;

/* Every part's names, in an api 2 mod's state (scripts.c, as it loads). */
void khdays_api_v2_open(KhdaysMod *mod);

/* What the mod holds let go: it stopped, was turned off or is reloaded. */
void khdays_api_v2_release(KhdaysMod *mod);

/* For a name of version 2 that changes the game: 1 when this mod may now, the game being `where`
 * (KhdaysWhere), else pushes nil and the reason and returns 0. */
int khdays_api_v2_may(struct lua_State *L, int where);

/* The enemies (api_enemies.c). */
void khdays_api_enemies_open(KhdaysMod *mod);
void khdays_api_enemies_release(KhdaysMod *mod);

/* The screens and title entries (api_screens.c); the answers delivered at the frame point. */
void khdays_api_screens_open(KhdaysMod *mod);
void khdays_api_screens_release(KhdaysMod *mod);
void khdays_api_screens_frame(void);

/* The deck (api_deck.c). */
void khdays_api_deck_open(KhdaysMod *mod);

/* Time, music, the draws and sharing (api_time.c): the timers fired and the shares heard at the frame
 * point; every mod's draws started again from the room's seed as a mission starts. */
void khdays_api_time_open(KhdaysMod *mod);
void khdays_api_time_release(KhdaysMod *mod);
void khdays_api_time_frame(void);
void khdays_api_time_mission_start(void);
/* A `shared` event's share (its `a`): whether it is for `mod`, and its table pushed. */
int khdays_api_time_shared_for(const KhdaysMod *mod, int share);
void khdays_api_time_push_shared(struct lua_State *L, int share);

/* The game's memory by address (api_memory.c). */
void khdays_api_memory_open(KhdaysMod *mod);

/* The run's end (api_run.c): the player's defeat a mod holds. */
void khdays_api_run_open(KhdaysMod *mod);
void khdays_api_run_release(KhdaysMod *mod);
/* The mod holding the defeat, NULL for none (the game's own). */
const KhdaysMod *khdays_api_run_holder(void);
/* Ov022_ActorSetHp's join: the HP `entity` is about to be given; 1 instead of a defeat of the local
 * player while a mod holds it (the game's own refusal of a defeat), the holder told. */
int khdays_scripts_lethal(void *entity, int value);

/* The handle `actor` has for `mod` (one it made), 0 without. */
int khdays_api_enemies_handle(const KhdaysMod *mod, unsigned actor);

/* What the game's side tells (enemies_link.c): an enemy defeated (the mission's or a mod's: its event
 * raised, a mod's handle then gone), one sent away settled, the area's enemies let go. */
void khdays_api_enemies_defeated(unsigned actor, int kind);
void khdays_api_enemies_gone(unsigned actor);
void khdays_api_enemies_field_left(void);

#endif
