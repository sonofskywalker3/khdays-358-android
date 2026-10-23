/* The run's end (api_v2.h): the mission left as the pause's Give Up leaves it, and the player's defeat a
 * mod holds. While a mod holds it, the hit that would take the local player's HP to 0 leaves 1 -- what
 * Ov022_ActorSetHp itself does for a host's peer in Mission Mode -- so the downed state and the game
 * over never come, and the mod hears `defeat`. One mod holds it at a time: the first that asks. */
#include "api_v2.h"

#include "api.h"
#include "events.h"
#include "game_link.h"
#include "sandbox.h"

#include <lauxlib.h>
#include <lua.h>

static KhdaysMod *holder;

const KhdaysMod *khdays_api_run_holder(void)
{
    return holder != NULL && holder->state == KHDAYS_MOD_LOADED ? holder : NULL;
}

/* player.take_defeat(on) */
static int take_defeat(lua_State *L)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    const KhdaysMod *held = khdays_api_run_holder();
    luaL_checktype(L, 1, LUA_TBOOLEAN);
    if (!khdays_api_may_change(L, 0)) {
        return 2;
    }
    if (held != NULL && held != m) {
        lua_pushnil(L);
        lua_pushfstring(L, "taken by %s", held->manifest.id);
        return 2;
    }
    holder = lua_toboolean(L, 1) ? m : NULL;
    lua_pushboolean(L, 1);
    return 1;
}

/* mission.start(number) */
static int start_mission(lua_State *L)
{
    const lua_Integer number = luaL_checkinteger(L, 1);
    const char *why;
    if (!lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
    }
    if (!khdays_api_may_change(L, 1)) {
        return 2;
    }
    why = number < 1 || number > 9999 ? "unknown mission" : khdays_link_start_mission((int)number);
    if (why != NULL) {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* mission.leave() */
static int leave(lua_State *L)
{
    const char *why;
    if (!khdays_api_may_change(L, 1)) {
        return 2;
    }
    why = khdays_link_leave_mission();
    if (why != NULL) {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int khdays_scripts_lethal(void *entity, int value)
{
    KhdaysEvent e = {KHDAYS_EVENT_DEFEAT, 0, 0, -1, 0, NULL};
    unsigned attacker;
    int hp, max;
    if (value > 0 || khdays_api_run_holder() == NULL || !khdays_link_is_player(entity)) {
        return value;
    }
    khdays_link_entity_hp(entity, &hp, &max);
    if (hp <= 0) {
        return value;
    }
    e.c = khdays_link_attacker(&attacker);
    e.a = hp - value;
    e.b = (int)attacker;
    khdays_script_events_raise(&e);
    return 1;
}

/* player.set_position(x, y, z): the local player put there, as the game's scripts put a player. */
static int set_position(lua_State *L)
{
    float at[3];
    const char *why;
    for (int i = 0; i < 3; ++i) {
        const lua_Number v = luaL_checknumber(L, i + 1);
        if (!(v > -500000 && v < 500000)) {
            return luaL_argerror(L, i + 1, "a place in the field's units");
        }
        at[i] = (float)v;
    }
    if (!khdays_api_v2_may(L, KHDAYS_WHERE_FIELD)) {
        return 2;
    }
    why = khdays_link_set_position(at);
    if (why != NULL) {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

void khdays_api_run_open(KhdaysMod *m)
{
    lua_getglobal(m->L, "player");
    lua_pushcfunction(m->L, take_defeat);
    lua_setfield(m->L, -2, "take_defeat");
    lua_pushcfunction(m->L, set_position);
    lua_setfield(m->L, -2, "set_position");
    lua_pop(m->L, 1);
    lua_getglobal(m->L, "mission");
    lua_pushcfunction(m->L, start_mission);
    lua_setfield(m->L, -2, "start");
    lua_pushcfunction(m->L, leave);
    lua_setfield(m->L, -2, "leave");
    lua_pop(m->L, 1);
}

void khdays_api_run_release(KhdaysMod *m)
{
    if (holder == m) {
        holder = NULL;
    }
}
