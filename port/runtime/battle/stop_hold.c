/* The Stop magic's hold, apart from the game (stop_hold.h). */
#include "stop_hold.h"

#include <stdlib.h>
#include <string.h>

typedef struct Held {
    unsigned enemy;
    unsigned cast; /* the cast that took it last: each takes it once */
    int ticks;     /* left; 0 once released, its damage still to deal */
    int kept;      /* the damage its hits would have dealt */
} Held;

static Held *held;
static int held_count, held_room;
static int zone_x, zone_z, zone_reach, zone_ticks, zone_on;
static unsigned zone_frame, zone_cast, zone_caster;

static Held *held_of(unsigned enemy)
{
    for (int i = 0; i < held_count; ++i) {
        if (held[i].enemy == enemy) {
            return &held[i];
        }
    }
    return NULL;
}

static Held *hold_new(unsigned enemy)
{
    if (held_count == held_room) {
        const int room = held_room > 0 ? 2 * held_room : 8;
        Held *grown = realloc(held, (size_t)room * sizeof(*held));
        if (grown == NULL) {
            return NULL;
        }
        held = grown;
        held_room = room;
    }
    Held *h = &held[held_count++];
    memset(h, 0, sizeof(*h));
    h->enemy = enemy;
    return h;
}

static void hold_drop(Held *h)
{
    *h = held[--held_count];
}

void khdays_stop_hold_cast(int x, int z, int reach, int ticks, unsigned frame, unsigned caster)
{
    zone_caster = caster;
    zone_x = x;
    zone_z = z;
    zone_reach = reach;
    zone_ticks = ticks;
    zone_frame = frame;
    zone_on = 1;
    ++zone_cast;
}

/* The ticks the zone holds `e` for: the cast's less its resistance, none when out of reach or untouchable. */
static int zone_ticks_for(const KhdaysStopEnemy *e)
{
    const long long dx = (long long)e->x - zone_x, dz = (long long)e->z - zone_z;
    const int resist = e->resist > 0 ? e->resist : 0;
    if (!e->standing || e->untouchable || resist >= 0x1000 || dx * dx + dz * dz > (long long)zone_reach * zone_reach) {
        return 0;
    }
    return (int)((long long)zone_ticks * (0x1000 - resist) >> 12);
}

int khdays_stop_hold_tick(unsigned enemy, const KhdaysStopEnemy *e, unsigned frame, int *taken, int *freed)
{
    Held *h = held_of(enemy);
    *taken = 0;
    *freed = 0;
    if (zone_on && frame - zone_frame > KHDAYS_STOP_ZONE_FRAMES) {
        zone_on = 0;
    }
    if (zone_on) {
        const int ticks = enemy != zone_caster ? zone_ticks_for(e) : 0;
        if (ticks > 0 && (h != NULL || (h = hold_new(enemy)) != NULL) && h->cast != zone_cast) {
            h->cast = zone_cast;
            if (h->ticks < ticks) {
                *taken = ticks;
                h->ticks = ticks;
            }
        }
    }
    if (h == NULL || h->ticks == 0) {
        return 0;
    }
    *freed = --h->ticks == 0;
    return 1;
}

int khdays_stop_hold_held(unsigned enemy)
{
    const Held *h = held_of(enemy);
    return h != NULL && h->ticks > 0;
}

int khdays_stop_hold_kept(unsigned enemy)
{
    const Held *h = held_of(enemy);
    return h != NULL ? h->kept : 0;
}

int khdays_stop_hold_store(unsigned enemy, int amount)
{
    Held *h = held_of(enemy);
    if (h == NULL || h->ticks == 0 || amount <= 0) {
        return amount;
    }
    h->kept += amount;
    return 0;
}

int khdays_stop_hold_due(unsigned enemy, int *amount)
{
    Held *h = held_of(enemy);
    if (h == NULL || h->ticks > 0) {
        return 0;
    }
    if (h->kept <= 0) {
        hold_drop(h);
        return 0;
    }
    *amount = h->kept;
    return 1;
}

void khdays_stop_hold_forget(unsigned enemy)
{
    Held *h = held_of(enemy);
    if (h != NULL) {
        hold_drop(h);
    }
}

void khdays_stop_hold_clear(void)
{
    held_count = 0;
    zone_on = 0;
}
