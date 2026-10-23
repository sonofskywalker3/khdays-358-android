/* The Stop magic's hold, apart from the game (stop_hold.c): the zone a cast marks, the enemies it holds and the damage
 * each keeps. stop_magic.c reads the game's memory for it and logs what happens. */
#ifndef KHDAYS_STOP_HOLD_H
#define KHDAYS_STOP_HOLD_H

#ifdef __cplusplus
extern "C" {
#endif

/* What the game says of an enemy on its tick. */
typedef struct KhdaysStopEnemy {
    int x, z;        /* its place on the ground, 20.12 fixed point */
    int standing;    /* set going, not one the area keeps idle */
    int untouchable; /* not to be hit, or freezing cannot touch it */
    int resist;      /* its resistance to freezing, 0x1000 whole */
} KhdaysStopEnemy;

/* A zone stays this many frames (two battle ticks), so every enemy ticks once in it. */
#define KHDAYS_STOP_ZONE_FRAMES 4

/* A cast by `caster`: the zone round (x, z), `reach` far (20.12), holding for `ticks` what its next ticks find in it
 * but the caster. */
void khdays_stop_hold_cast(int x, int z, int reach, int ticks, unsigned frame, unsigned caster);

/* An enemy's tick at `frame` (or another player's, `enemy` its own number): 1 while it is held. `*taken` the ticks it
 * was just held for (0 when not just taken; a second cast holds it again for the longer of the two, never their sum),
 * `*freed` 1 when its hold ran out. */
int khdays_stop_hold_tick(unsigned enemy, const KhdaysStopEnemy *e, unsigned frame, int *taken, int *freed);

/* Whether the enemy is held; the damage it keeps. */
int khdays_stop_hold_held(unsigned enemy);
int khdays_stop_hold_kept(unsigned enemy);

/* A hit's damage on the enemy: kept while it is held, the damage to deal now returned (0 then). */
int khdays_stop_hold_store(unsigned enemy, int amount);

/* A released enemy's kept damage: 1 with `*amount` to deal now, 0 for none (an enemy released keeping none is
 * forgotten). */
int khdays_stop_hold_due(unsigned enemy, int *amount);

/* An enemy dealt its kept damage, or gone; every hold and the zone (an area left). */
void khdays_stop_hold_forget(unsigned enemy);
void khdays_stop_hold_clear(void);

#ifdef __cplusplus
}
#endif

#endif
