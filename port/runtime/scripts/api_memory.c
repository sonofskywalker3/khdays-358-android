/* The game's memory by address (api_v2.h; docs/SCRIPTING.md, "The game's memory"): read by any mod of
 * version 2, written by a gameplay mod whose mod.ini says memory = write, never by a guest. What lies
 * where is the decompilation's; game_link.c keeps every access inside the game's memory. Numbers are
 * little-endian, as on the DS. */
#include "api_v2.h"

#include "api.h"
#include "game_link.h"
#include "sandbox.h"

#include <lauxlib.h>
#include <lua.h>

#include <limits.h>
#include <stdio.h>

#define OUTSIDE "not the game's memory"

static int refuse(lua_State *L, const char *why)
{
    lua_pushnil(L);
    lua_pushstring(L, why);
    return 2;
}

/* Argument `arg` as an address; 0 when it is none of the window's 32-bit references. */
static int address_of(lua_State *L, int arg, unsigned *address)
{
    const lua_Integer a = luaL_checkinteger(L, arg);
    *address = (unsigned)a;
    return a >= 0 && a <= 0xffffffffLL;
}

/* memory.u8 .. memory.s32: upvalue 1 the size in bytes, 2 whether signed. */
static int read_number(lua_State *L)
{
    const int size = (int)lua_tointeger(L, lua_upvalueindex(1)), is_signed = lua_toboolean(L, lua_upvalueindex(2));
    unsigned char bytes[4];
    unsigned address, v = 0;
    if (!address_of(L, 1, &address) || !khdays_link_read(address, bytes, (size_t)size)) {
        return refuse(L, OUTSIDE);
    }
    for (int i = size - 1; i >= 0; --i) {
        v = v << 8 | bytes[i];
    }
    if (is_signed) {
        lua_pushinteger(L, size == 1 ? (signed char)v : size == 2 ? (short)v : (int)v);
    } else {
        lua_pushinteger(L, v);
    }
    return 1;
}

/* For a write: 1 when this mod may now, else pushes nil and the reason and returns 0. */
static int may_write(lua_State *L)
{
    if (!khdays_api_may_change(L, 0)) {
        return 0;
    }
    if (!khdays_sandbox_mod(L)->manifest.memory_write) {
        lua_pushnil(L);
        lua_pushstring(L, "mod.ini has no memory = write");
        return 0;
    }
    return 1;
}

/* memory.set_u8 .. set_u32: upvalue 1 the size; the value unsigned or signed, as it fits. */
static int write_number(lua_State *L)
{
    const int size = (int)lua_tointeger(L, lua_upvalueindex(1));
    const lua_Integer v = luaL_checkinteger(L, 2), bits = 8 * size;
    unsigned char bytes[4];
    unsigned address;
    const int is_address = address_of(L, 1, &address);
    luaL_argcheck(L, v >= -(1LL << (bits - 1)) && v < (1LL << bits), 2, "does not fit in the size written");
    if (!may_write(L)) {
        return 2;
    }
    for (int i = 0; i < size; ++i) {
        bytes[i] = (unsigned char)((unsigned long long)v >> (8 * i));
    }
    if (!is_address || !khdays_link_write(address, bytes, (size_t)size)) {
        return refuse(L, OUTSIDE);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int bytes(lua_State *L)
{
    const lua_Integer n = luaL_checkinteger(L, 2);
    unsigned address;
    luaL_Buffer b;
    luaL_argcheck(L, n >= 0, 2, "a count of bytes");
    if (!address_of(L, 1, &address) || !khdays_link_reachable(address, (unsigned long long)n)) {
        return refuse(L, OUTSIDE);
    }
    khdays_link_read(address, luaL_buffinitsize(L, &b, (size_t)n), (size_t)n);
    luaL_pushresultsize(&b, (size_t)n);
    return 1;
}

static int set_bytes(lua_State *L)
{
    size_t n;
    const char *s = luaL_checklstring(L, 2, &n);
    unsigned address;
    const int is_address = address_of(L, 1, &address);
    if (!may_write(L)) {
        return 2;
    }
    if (!is_address || !khdays_link_write(address, s, n)) {
        return refuse(L, OUTSIDE);
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int loaded(lua_State *L)
{
    const lua_Integer id = luaL_checkinteger(L, 1);
    lua_pushboolean(L, id >= 0 && id <= INT_MAX && khdays_link_overlay_loaded((int)id));
    return 1;
}

void khdays_api_memory_open(KhdaysMod *m)
{
    static const struct {
        const char *name;
        int size, is_signed;
    } numbers[] = {{"u8", 1, 0}, {"u16", 2, 0}, {"u32", 4, 0}, {"s8", 1, 1}, {"s16", 2, 1}, {"s32", 4, 1}};
    static const luaL_Reg functions[] = {{"bytes", bytes}, {"set_bytes", set_bytes}, {"loaded", loaded}, {NULL, NULL}};
    lua_State *L = m->L;
    luaL_newlib(L, functions);
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        lua_pushinteger(L, numbers[i].size);
        lua_pushboolean(L, numbers[i].is_signed);
        lua_pushcclosure(L, read_number, 2);
        lua_setfield(L, -2, numbers[i].name);
        if (!numbers[i].is_signed) {
            char name[16];
            lua_pushinteger(L, numbers[i].size);
            lua_pushcclosure(L, write_number, 1);
            snprintf(name, sizeof(name), "set_%s", numbers[i].name);
            lua_setfield(L, -2, name);
        }
    }
    lua_setglobal(L, "memory");
}
