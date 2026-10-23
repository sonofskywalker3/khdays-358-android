/* The Stop magic (runtime/battle/): its data file as compiled in, a file missing a part, and the hold. */
#include "../runtime/battle/stop_data.h"
#include "../runtime/battle/stop_hold.h"

#include <stdio.h>
#include <string.h>

extern const unsigned char khdays_stop_magic_text[];
extern const unsigned int khdays_stop_magic_text_size;

static int failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                                                 \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static void the_compiled_file(void)
{
    KhdaysStopData d;
    char why[160] = "";
    const int bad =
        khdays_stop_data_read((const char *)khdays_stop_magic_text, khdays_stop_magic_text_size, &d, why, sizeof(why));
    if (bad != 0) {
        fprintf(stderr, "stop_magic.txt: %s\n", why);
    }
    CHECK(bad == 0);
    CHECK(khdays_stop_data_panels_whole(&d));
    CHECK(d.levels[0].id == 628 && d.levels[1].id == 629 && d.levels[2].id == 630);
    CHECK(d.levels[0].ticks < d.levels[1].ticks && d.levels[1].ticks < d.levels[2].ticks);
    CHECK(d.magic_lv == 25);
    /* the recipes: dearer than every other magic's (Cure's 1600, 2000, 2600), each level after the one before */
    CHECK(khdays_stop_data_shop_whole(&d));
    CHECK(d.recipes[0].munny > 1600 && d.recipes[1].munny > 2000 && d.recipes[2].munny > 2600);
    CHECK(d.recipes[1].materials[0].id == d.levels[0].id && d.recipes[2].materials[0].id == d.levels[1].id);
    CHECK(d.recipes[2].material_count == KHDAYS_STOP_INGREDIENTS);
    /* the game's own names where it has them (UI/btl/<lang>/magic.s.z), the port's where it left them empty */
    CHECK(strcmp(d.words[0][0].name, "Stop") == 0 && strcmp(d.words[0][2].name, "Stopga") == 0);
    CHECK(strcmp(d.words[2][1].name, "Stop +") == 0 && strcmp(d.words[2][2].name, "Stop X") == 0);
    CHECK(strcmp(d.words[1][0].name, "Paro") == 0 && strcmp(d.words[1][2].name, "Paro++") == 0);
    /* every language's words, none empty */
    for (int g = 0; g < KHDAYS_STOP_LANGUAGES; ++g) {
        for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
            const KhdaysStopWords *w = &d.words[g][l];
            CHECK(w->name[0] != '\0' && w->panel[0] != '\0' && w->magic[0] != '\0');
        }
    }
    /* the game's highlight and line breaks */
    CHECK(strchr(d.words[1][0].panel, '\x02') != NULL && strchr(d.words[1][0].panel, '\x01') != NULL);
    CHECK(strchr(d.words[0][0].magic, '\n') != NULL && strstr(d.words[0][0].magic, "\\n") == NULL);
    khdays_stop_data_free(&d);
}

static void a_part_missing(void)
{
    static const char text[] = "level 1 628 120 5 300\nlevel 2 629 180 5.5 400\nlevel 3 630 240 6 500\n"
                               "name en 1 Stop\n";
    static const char bad[] = "level 1 628 120 5 300\nlevel 4 631 1 1 1\n";
    KhdaysStopData d;
    char why[160];
    CHECK(khdays_stop_data_read(text, sizeof(text) - 1, &d, why, sizeof(why)) == 0);
    CHECK(!khdays_stop_data_panels_whole(&d)); /* words missing: no panels */
    CHECK(!khdays_stop_data_shop_whole(&d) && d.magic_lv == 0);
    khdays_stop_data_free(&d);
    CHECK(khdays_stop_data_read(bad, sizeof(bad) - 1, &d, why, sizeof(why)) == 2); /* no fourth level */
    khdays_stop_data_free(&d);
    static const char five[] = "recipe 1 100 1x1 2x1 3x1 4x1 5x1\n"; /* a recipe's record holds four ingredients */
    CHECK(khdays_stop_data_read(five, sizeof(five) - 1, &d, why, sizeof(why)) == 1);
    khdays_stop_data_free(&d);
}

/* --- The hold (stop_hold.c) --- */

#define UNIT 4096

static KhdaysStopEnemy standing_at(int x, int z)
{
    KhdaysStopEnemy e = {x, z, 1, 0, 0};
    return e;
}

/* Ticks enemy `id` at `e` from `frame` until its hold runs out; the ticks it was held. */
static int ticks_held(unsigned id, const KhdaysStopEnemy *e, unsigned frame)
{
    int taken, freed, n = 0;
    while (khdays_stop_hold_tick(id, e, frame, &taken, &freed)) {
        ++n;
        frame += 2;
        if (freed) {
            break;
        }
    }
    return n;
}

static void each_level_holds_its_ticks(void)
{
    static const int ticks[3] = {120, 180, 240};
    for (int l = 0; l < 3; ++l) {
        const KhdaysStopEnemy e = standing_at(2 * UNIT, 0);
        khdays_stop_hold_clear();
        khdays_stop_hold_cast(0, 0, 5 * UNIT, ticks[l], 100, 0xcafe);
        CHECK(ticks_held(7, &e, 100) == ticks[l]);
        CHECK(!khdays_stop_hold_held(7));
    }
}

static void who_is_held(void)
{
    int taken, freed;
    KhdaysStopEnemy far = standing_at(6 * UNIT, 0), idle = standing_at(UNIT, 0), immune = standing_at(UNIT, 0),
                    tough = standing_at(UNIT, 0), whole = standing_at(UNIT, 0);
    idle.standing = 0;
    immune.untouchable = 1;
    tough.resist = 0x800; /* half */
    whole.resist = 0x1000;
    khdays_stop_hold_clear();
    khdays_stop_hold_cast(0, 0, 5 * UNIT, 120, 10, 0xcafe);
    CHECK(!khdays_stop_hold_tick(1, &far, 10, &taken, &freed) && taken == 0);
    CHECK(!khdays_stop_hold_tick(2, &idle, 10, &taken, &freed));
    CHECK(!khdays_stop_hold_tick(3, &immune, 10, &taken, &freed));
    CHECK(!khdays_stop_hold_tick(5, &whole, 10, &taken, &freed));
    CHECK(khdays_stop_hold_tick(4, &tough, 10, &taken, &freed) && taken == 60);
    /* never its caster */
    CHECK(!khdays_stop_hold_tick(0xcafe, &tough, 10, &taken, &freed));
    /* the zone is gone two ticks after the cast */
    const KhdaysStopEnemy near = standing_at(0, 0);
    CHECK(!khdays_stop_hold_tick(6, &near, 10 + KHDAYS_STOP_ZONE_FRAMES + 1, &taken, &freed));
}

static void a_second_cast_restarts(void)
{
    int taken, freed;
    const KhdaysStopEnemy e = standing_at(0, 0);
    khdays_stop_hold_clear();
    khdays_stop_hold_cast(0, 0, 5 * UNIT, 120, 0, 0xcafe);
    CHECK(khdays_stop_hold_tick(9, &e, 0, &taken, &freed) && taken == 120);
    for (unsigned f = 2; f < 200; f += 2) {
        khdays_stop_hold_tick(9, &e, f, &taken, &freed); /* 100 ticks gone, 20 left */
    }
    khdays_stop_hold_cast(0, 0, 5 * UNIT, 120, 200, 0xcafe);
    CHECK(khdays_stop_hold_tick(9, &e, 200, &taken, &freed) && taken == 120); /* again 120, not 140 */
    CHECK(ticks_held(9, &e, 202) == 119);
}

static void kept_damage_dealt_once(void)
{
    int taken, freed, amount = 0;
    const KhdaysStopEnemy e = standing_at(0, 0);
    khdays_stop_hold_clear();
    CHECK(khdays_stop_hold_store(4, 8) == 8); /* not held: dealt now */
    khdays_stop_hold_cast(0, 0, 5 * UNIT, 3, 0, 0xcafe);
    CHECK(khdays_stop_hold_tick(4, &e, 0, &taken, &freed));
    CHECK(khdays_stop_hold_store(4, 8) == 0 && khdays_stop_hold_store(4, 9) == 0 && khdays_stop_hold_store(4, 0) == 0);
    CHECK(khdays_stop_hold_kept(4) == 17);
    CHECK(!khdays_stop_hold_due(4, &amount)); /* still held */
    CHECK(khdays_stop_hold_tick(4, &e, 10, &taken, &freed) && !freed);
    CHECK(khdays_stop_hold_tick(4, &e, 12, &taken, &freed) && freed);
    CHECK(khdays_stop_hold_store(4, 5) == 5); /* moving again: dealt now */
    CHECK(khdays_stop_hold_due(4, &amount) && amount == 17);
    CHECK(khdays_stop_hold_due(4, &amount) && amount == 17); /* again until dealt (a hit's grace drops it) */
    khdays_stop_hold_forget(4);
    CHECK(!khdays_stop_hold_due(4, &amount) && khdays_stop_hold_kept(4) == 0);
}

static void gone_enemies_drop_their_damage(void)
{
    int taken, freed, amount;
    const KhdaysStopEnemy e = standing_at(0, 0);
    khdays_stop_hold_clear();
    khdays_stop_hold_cast(0, 0, 5 * UNIT, 120, 0, 0xcafe);
    CHECK(khdays_stop_hold_tick(1, &e, 0, &taken, &freed) && khdays_stop_hold_tick(2, &e, 0, &taken, &freed));
    khdays_stop_hold_store(1, 30);
    khdays_stop_hold_store(2, 40);
    khdays_stop_hold_forget(1);
    CHECK(!khdays_stop_hold_held(1) && khdays_stop_hold_kept(1) == 0 && khdays_stop_hold_kept(2) == 40);
    khdays_stop_hold_clear(); /* the area left */
    CHECK(!khdays_stop_hold_held(2) && !khdays_stop_hold_due(2, &amount));
    /* released with nothing kept: forgotten */
    khdays_stop_hold_cast(0, 0, 5 * UNIT, 1, 0, 0xcafe);
    CHECK(khdays_stop_hold_tick(3, &e, 0, &taken, &freed) && freed);
    CHECK(!khdays_stop_hold_due(3, &amount) && !khdays_stop_hold_held(3));
}

int main(void)
{
    the_compiled_file();
    a_part_missing();
    each_level_holds_its_ticks();
    who_is_held();
    a_second_cast_restarts();
    kept_damage_dealt_once();
    gone_enemies_drop_their_damage();
    if (failures == 0) {
        printf("stop magic: all checks passed\n");
    }
    return failures != 0;
}
