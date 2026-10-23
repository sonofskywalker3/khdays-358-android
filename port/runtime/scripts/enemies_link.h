/* The field's enemies as the mods' interface reaches them (api_enemies.c): the game's side, joined to
 * the routines that make, defeat and release them (prepare.py's HOOKS). docs/RUNTIME_MAP.md, "The field's
 * enemies". KHDAYS_TRACE_ENEMIES=1 traces what the joins see. */
#ifndef KHDAYS_SCRIPTS_ENEMIES_LINK_H
#define KHDAYS_SCRIPTS_ENEMIES_LINK_H

typedef struct KhdaysEnemyInfo {
    int kind, hp, max_hp, level;
    float at[3];
} KhdaysEnemyInfo;

/* The kinds this area makes (the rows of the mission's enemy table the area gave a place): up to `room`
 * of them into `out` (NULL to count); how many there are, 0 where the area has no enemies. */
int khdays_enemies_kinds(int *out, int room);

/* A kind's name as the decompilation names its enemy (enemy_names.c); NULL for none. How many kinds the
 * game has (ov107's table of their overlays). */
const char *khdays_enemies_name(int kind);
int khdays_enemies_kind_count(void);

/* Whether an enemy of `kind` can be made here now (NULL), else why not: the area makes it, or its
 * overlay is loaded or still fits among the six places the game keeps for the enemies' overlays
 * (Ov107_SpawnEntityClass, which stops the game past them). */
const char *khdays_enemies_can_make(int kind);

/* One enemy of `kind` made by the game's routine at `at` (the field's units), its level the area's for
 * that kind (the mission's for a kind the area does not make) plus `level`, its HP and attacks times `hp`
 * and `strength`, and set going; its actor, or 0 and why. */
unsigned khdays_enemies_make(int kind, const float at[3], int level, double hp, double strength, const char **why);

/* Sends a standing enemy away as a spawner does when the player leaves it (no defeat, no reward); 0
 * when it no longer stands. */
int khdays_enemies_remove(unsigned actor);

/* A standing enemy's kind, HP, level and place; 0 when it no longer stands. */
int khdays_enemies_info(unsigned actor, KhdaysEnemyInfo *out);

/* The joins. */
void khdays_enemies_marker_made(int source, int row, void *actor, int id); /* Ov002_CreateActorFromMarker */
void khdays_enemies_class(int id);                                         /* Ov107_SpawnEntityClass */
int khdays_enemies_spawner_run(void *spawner, int op);                     /* Ov107_Spawner_Run: 1 to skip it */
void khdays_enemies_retired(void *actor);                                  /* Ov107_Spawner_OnActorRetired */
void khdays_enemies_defeated(void *actor);                                 /* Ov107_AiState_OnDefeat */
void khdays_enemies_area_left(void);                                       /* Ov002_ReleaseResources */

/* While `on`, the mission's own spawners set none of their idle enemies going (their op 0 skipped): the
 * mission's areas then never end by their enemies. Those standing stay. */
void khdays_enemies_hold_mission(int on);

/* The frame point (scripts.c): the enemies sent away settled, the field left. */
void khdays_enemies_frame(void);

#endif
