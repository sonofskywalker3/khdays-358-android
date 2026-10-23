/* The Stop magic, the game's side (stop_magic.c): Days's unfinished sixth magic made whole on what the game left
 * (its effect, its places in the battle's tables, its English and French names). docs/RUNTIME_MAP.md, "The magic". */
#ifndef KHDAYS_STOP_MAGIC_H
#define KHDAYS_STOP_MAGIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Whether Stop is in the game ([gameplay] stop_magic, host/app/profile.h): set before the game starts. Off, every
 * function below does nothing and answers as without Stop, the game as the DS's (a save's Stop panels kept unseen, as
 * an older port keeps them). */
void khdays_stop_set_enabled(int on);
int khdays_stop_enabled(void);

/* Whether `id` is one of Stop's panels, in the game (its records written). Called from the game's C. */
int khdays_stop_panel(int id);

/* Message database `id` loaded (MsgDb_LoadDb): Stop's records and words written into it. Called from the game's C. */
void khdays_stop_db_loaded(int id);
/* The panels' pictures loaded (ui/pnl/3d_&.pak.z, its archive relocated): the clock Stop's panels show takes the
 * magic panels' ground. */
void khdays_stop_pictures(unsigned int archive);
/* A game file loaded (Archive_LoadFile: its place in the ROM, its bytes): the shop's 2D pictures' clock the same. */
void khdays_stop_file_loaded(unsigned top, void *buffer, unsigned size);

/* The battle's list of magic names loaded (Ov002_CreatePanelSession, its record at `record`): the names the game
 * left empty filled. Called from the game's C. */
void khdays_stop_magic_names(void *record);

/* An enemy gone (Ov107_Spawner_OnActorRetired): its hold dropped. The area left (Ov002_ReleaseResources): every hold
 * and the zone dropped. */
void khdays_stop_enemy_gone(void *enemy);
void khdays_stop_area_left(void);

/* The battle, called from the game's C (prepare.py's joins); `pool` is an actor's pool of magic slots, a reference.
 * The cast's level animation entry the pool kept in the word after its 15 slots, the port's now so that word is
 * Stop's place: read (0 for none) and set (0 forgets it). */
int khdays_magic_track(unsigned int pool);
void khdays_magic_track_set(unsigned int pool, int entry);
/* Stop's slot: whether the actor gets one and from which model and template (Ov022_BuildSlotPool: the local player
 * with Stop's panels set; in a room every player); a cast's slot (Ov022_SetRecordValue): each of Stop's levels, cells
 * 15-17, casts Stop's slot, 15, its level kept. */
int khdays_stop_slot_wanted(int actor, char *path, int *parts, int *template_index);
int khdays_stop_record(unsigned int pool, int value);
/* The slot the cast's message names (Ov022_MarshalStateHalf12): Stop's as its level's cell, which the copies that
 * receive it take with khdays_stop_record (Ov022_OnEndMessage). */
int khdays_stop_message_slot(unsigned int pool, int slot);
/* The submenu: its cells and their mask (Ov002_CreatePanelSession), each level's casts on the HUD
 * (Ov022_UpdateCommandInput), the casts a level has left (Ov022_IsSlotActionAllowed). */
int khdays_stop_cells(void *session, int count);
int khdays_stop_cell_mask(void);
void khdays_stop_hud(int actor, int *mask);
int khdays_stop_casts_left(unsigned int pool, int level);
/* A cast begun (Ov022_BeginSubStateDispatch): 1 when it is Stop's, its count spent and its zone marked. Its level's
 * animation for the path the game made from Stop's place, its slot forgetting the one bound before
 * (Ov022_BuildEntryLabelText). */
int khdays_stop_cast(unsigned int pool);
void khdays_stop_animation(unsigned int pool, char *path);
/* An enemy's tick (Ov107_ProcessObjectTick): whether it is held now (and the zone takes it), whether it is held,
 * whether its kept damage is to be dealt now (and how much) and that the hit was made. */
int khdays_stop_holding(void *enemy);
/* A player's tick (Ov022_ActorUpdate): whether another's Stop holds it now, the room's friendly fire on. */
int khdays_stop_player_holding(void *actor);
int khdays_stop_held(void *enemy);
int khdays_stop_release(void *enemy, int *amount);
void khdays_stop_released(void *enemy);
/* A hit (Ov107_AiState_ApplyHit): its damage measured, kept while the enemy is held (what is left to deal), and the
 * damage the enemy's own handler reckons (Ov107_CalcHitDamage: none while held). */
void khdays_stop_measure(int on);
int khdays_stop_store(void *enemy, int amount);
int khdays_stop_damage(void *enemy, int amount);

#ifdef __cplusplus
}
#endif

#endif
