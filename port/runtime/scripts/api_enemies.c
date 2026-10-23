/* The field's enemies a mod makes and hears (api_v2.h; enemies_link.h has the game's side). A mod's enemy
 * keeps its handle while it stands; once defeated, sent away or let go with its area it is gone (kept,
 * marked, until its events are delivered, so its maker still knows it). */
#include "api_v2.h"

#include "enemies_link.h"
#include "events.h"
#include "game_link.h"
#include "sandbox.h"

#include <lauxlib.h>
#include <lua.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GONE 0x80000000u /* an entry's actor once gone: the actor's reference, marked */

static int next_handle; /* unique for the session, whichever mod made it */

static int refuse(lua_State *L, const char *why)
{
    lua_pushnil(L);
    lua_pushstring(L, why);
    return 2;
}

static KhdaysModEnemy *find(const KhdaysMod *m, int handle)
{
    for (int i = 0; m->v2 != NULL && i < m->v2->enemy_count; ++i) {
        if (m->v2->enemies[i].handle == handle) {
            return &m->v2->enemies[i];
        }
    }
    return NULL;
}

/* The entries gone and already heard of, dropped. */
static void tidy(KhdaysModV2 *v)
{
    int kept = 0;
    for (int i = 0; i < v->enemy_count; ++i) {
        if (!(v->enemies[i].actor & GONE)) {
            v->enemies[kept++] = v->enemies[i];
        }
    }
    v->enemy_count = kept;
}

static int add(KhdaysModV2 *v, unsigned actor, int kind)
{
    if (v->enemy_count == v->enemy_room) {
        const int room = v->enemy_room ? v->enemy_room * 2 : 16;
        KhdaysModEnemy *grown = realloc(v->enemies, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return 0;
        }
        v->enemies = grown;
        v->enemy_room = room;
    }
    v->enemies[v->enemy_count].handle = ++next_handle;
    v->enemies[v->enemy_count].actor = actor;
    v->enemies[v->enemy_count].kind = kind;
    return v->enemies[v->enemy_count++].handle;
}

/* enemies.kinds(): the kinds the area makes. enemies.kinds("all"): every kind of the game, `ready` when
 * one can be made here now. */
static int kinds(lua_State *L)
{
    const int all = lua_isstring(L, 1) && strcmp(lua_tostring(L, 1), "all") == 0;
    int count = all ? khdays_enemies_kind_count() : khdays_enemies_kinds(NULL, 0);
    int *list = count > 0 ? malloc((size_t)count * sizeof(*list)) : NULL;
    if (count > 0 && list == NULL) {
        return luaL_error(L, "not enough memory");
    }
    if (!lua_isnoneornil(L, 1) && !all) {
        free(list);
        return luaL_argerror(L, 1, "nothing, or \"all\"");
    }
    for (int i = 0; all && i < count; ++i) {
        list[i] = i;
    }
    count = all || list == NULL ? count : khdays_enemies_kinds(list, count);
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; ++i) {
        const char *name = khdays_enemies_name(list[i]);
        lua_createtable(L, 0, 3);
        lua_pushinteger(L, list[i]);
        lua_setfield(L, -2, "kind");
        lua_pushstring(L, name != NULL ? name : "");
        lua_setfield(L, -2, "name");
        if (all) {
            lua_pushboolean(L, khdays_enemies_can_make(list[i]) == NULL);
            lua_setfield(L, -2, "ready");
        }
        lua_rawseti(L, -2, i + 1);
    }
    free(list);
    return 1;
}

static double number_field(lua_State *L, const char *key, double fallback)
{
    double v = fallback;
    lua_getfield(L, 1, key);
    if (!lua_isnil(L, -1)) {
        if (!lua_isnumber(L, -1)) {
            luaL_error(L, "enemies.make: %s must be a number", key);
        }
        v = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);
    return v;
}

/* enemies.make{kind, at, count, level, hp, strength, spread}: the enemies made, round `at` when spread. */
static int make(lua_State *L)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    float centre[3];
    int kind, count, level, made = 0;
    double hp, strength, spread;
    const char *why = NULL;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "kind");
    if (!lua_isinteger(L, -1)) {
        return luaL_error(L, "enemies.make: kind must be a whole number (enemies.kinds())");
    }
    kind = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    count = (int)number_field(L, "count", 1);
    level = (int)number_field(L, "level", 0);
    hp = number_field(L, "hp", 1);
    strength = number_field(L, "strength", 1);
    spread = number_field(L, "spread", 0);
    if (count < 1) {
        return luaL_error(L, "enemies.make: count must be 1 or more");
    }
    if (!(hp > 0) || !(strength > 0) || spread < 0) {
        return luaL_error(L, "enemies.make: hp and strength above 0, spread 0 or more");
    }
    if (!khdays_api_v2_may(L, KHDAYS_WHERE_FIELD)) {
        return 2;
    }
    lua_getfield(L, 1, "at");
    if (lua_isstring(L, -1) && !lua_isnumber(L, -1)) {
        if (strcmp(lua_tostring(L, -1), "player") != 0 || !khdays_link_position(centre)) {
            return refuse(L, "no player");
        }
    } else if (lua_istable(L, -1)) {
        for (int i = 0; i < 3; ++i) {
            lua_rawgeti(L, -1, i + 1);
            if (!lua_isnumber(L, -1)) {
                return luaL_error(L, "enemies.make: at must be {x, y, z} or \"player\"");
            }
            centre[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    } else {
        return luaL_error(L, "enemies.make: at must be {x, y, z} or \"player\"");
    }
    lua_pop(L, 1);
    why = khdays_enemies_can_make(kind);
    if (why != NULL) {
        return refuse(L, why);
    }
    tidy(m->v2);
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; ++i) {
        const double turn = 6.283185307179586 * i / count;
        const float at[3] = {centre[0] + (float)(spread * cos(turn)), centre[1],
                             centre[2] + (float)(spread * sin(turn))};
        const unsigned actor = khdays_enemies_make(kind, at, level, hp, strength, &why);
        int handle;
        if (actor == 0) {
            break;
        }
        handle = add(m->v2, actor, kind);
        if (handle == 0) {
            khdays_enemies_remove(actor);
            why = "not enough memory";
            break;
        }
        lua_pushinteger(L, handle);
        lua_rawseti(L, -2, ++made);
        {
            const KhdaysEvent e = {KHDAYS_EVENT_ENEMY_APPEARED, (int)actor, kind, 0, 0, NULL};
            khdays_script_events_raise(&e);
        }
    }
    if (made == 0) {
        lua_pop(L, 1);
        return refuse(L, why != NULL ? why : "not now");
    }
    if (why != NULL) {
        lua_pushstring(L, why); /* the rest not made, and why */
        return 2;
    }
    return 1;
}

/* One handle removed; 0 when it was already gone. */
static int remove_one(KhdaysMod *m, int handle)
{
    KhdaysModEnemy *e = find(m, handle);
    if (e == NULL || (e->actor & GONE)) {
        return 0;
    }
    khdays_enemies_remove(e->actor);
    e->actor |= GONE;
    return 1;
}

static int remove_enemies(lua_State *L)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    int removed = 0;
    if (!khdays_api_v2_may(L, KHDAYS_WHERE_FIELD)) {
        return 2;
    }
    if (lua_istable(L, 1)) {
        const lua_Integer n = luaL_len(L, 1);
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, 1, i);
            removed += remove_one(m, (int)luaL_checkinteger(L, -1));
            lua_pop(L, 1);
        }
    } else {
        removed = remove_one(m, (int)luaL_checkinteger(L, 1));
    }
    if (removed == 0) {
        return refuse(L, "gone");
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int list(lua_State *L)
{
    const KhdaysMod *m = khdays_sandbox_mod(L);
    int n = 0;
    lua_newtable(L);
    for (int i = 0; m->v2 != NULL && i < m->v2->enemy_count; ++i) {
        if (!(m->v2->enemies[i].actor & GONE)) {
            lua_pushinteger(L, m->v2->enemies[i].handle);
            lua_rawseti(L, -2, ++n);
        }
    }
    return 1;
}

static int info(lua_State *L)
{
    const KhdaysModEnemy *e = find(khdays_sandbox_mod(L), (int)luaL_checkinteger(L, 1));
    KhdaysEnemyInfo out;
    if (e == NULL || (e->actor & GONE) || !khdays_enemies_info(e->actor, &out)) {
        return refuse(L, "gone");
    }
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, out.kind);
    lua_setfield(L, -2, "kind");
    lua_pushinteger(L, out.hp);
    lua_setfield(L, -2, "hp");
    lua_pushinteger(L, out.max_hp);
    lua_setfield(L, -2, "max_hp");
    lua_pushinteger(L, out.level);
    lua_setfield(L, -2, "level");
    lua_pushnumber(L, out.at[0]);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, out.at[1]);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, out.at[2]);
    lua_setfield(L, -2, "z");
    return 1;
}

static const KhdaysMod *holder; /* the mod holding the mission's own enemies back, NULL none */

/* enemies.hold_mission(on): while on, the mission's own enemies are not set going (its areas never end
 * by them); one mod holds them at a time. */
static int hold_mission(lua_State *L)
{
    const KhdaysMod *m = khdays_sandbox_mod(L);
    const int on = lua_toboolean(L, 1);
    luaL_checktype(L, 1, LUA_TBOOLEAN);
    if (!khdays_api_v2_may(L, KHDAYS_WHERE_ELSEWHERE)) {
        return 2;
    }
    if (holder != NULL && holder != m) {
        lua_pushnil(L);
        lua_pushfstring(L, "held by %s", holder->manifest.id);
        return 2;
    }
    holder = on ? m : NULL;
    khdays_enemies_hold_mission(on);
    lua_pushboolean(L, 1);
    return 1;
}

void khdays_api_enemies_open(KhdaysMod *m)
{
    static const luaL_Reg enemies[] = {{"kinds", kinds}, {"make", make}, {"remove", remove_enemies},
                                       {"list", list},   {"info", info}, {"hold_mission", hold_mission},
                                       {NULL, NULL}};
    luaL_newlib(m->L, enemies);
    lua_setglobal(m->L, "enemies");
}

void khdays_api_enemies_release(KhdaysMod *m)
{
    if (holder == m) {
        holder = NULL;
        khdays_enemies_hold_mission(0);
    }
    if (m->v2 != NULL) {
        free(m->v2->enemies);
        m->v2->enemies = NULL;
        m->v2->enemy_count = m->v2->enemy_room = 0;
    }
}

/* The handle `actor` has for `m` (0: none): the one standing, else the last gone (its events after). */
int khdays_api_enemies_handle(const KhdaysMod *m, unsigned actor)
{
    int gone = 0;
    for (int i = 0; m->v2 != NULL && i < m->v2->enemy_count; ++i) {
        if (m->v2->enemies[i].actor == actor) {
            return m->v2->enemies[i].handle;
        }
        if ((m->v2->enemies[i].actor & ~GONE) == actor) {
            gone = m->v2->enemies[i].handle;
        }
    }
    return gone;
}

/* Marks `actor` gone in every mod (its events already raised by the caller). */
static void mark_gone(unsigned actor)
{
    for (int i = 0; i < khdays_scripts_mod_count(); ++i) {
        KhdaysModV2 *v = khdays_scripts_mod(i)->v2;
        for (int k = 0; v != NULL && k < v->enemy_count; ++k) {
            if (v->enemies[k].actor == actor) {
                v->enemies[k].actor |= GONE;
            }
        }
    }
}

void khdays_api_enemies_defeated(unsigned actor, int kind)
{
    const KhdaysEvent e = {KHDAYS_EVENT_ENEMY_DEFEATED, (int)actor, kind, 0, 0, NULL};
    khdays_script_events_raise(&e);
    mark_gone(actor);
}

void khdays_api_enemies_gone(unsigned actor)
{
    mark_gone(actor);
}

void khdays_api_enemies_field_left(void)
{
    for (int i = 0; i < khdays_scripts_mod_count(); ++i) {
        KhdaysModV2 *v = khdays_scripts_mod(i)->v2;
        if (v != NULL) {
            v->enemy_count = 0;
        }
    }
}
