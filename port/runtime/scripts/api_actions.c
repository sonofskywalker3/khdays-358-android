/* The game's actions a mod may ask for (game_link.h has where each is done): items given and taken, a
 * sound, the field's notice. A refused action returns nil and its reason. */
#include "api.h"

#include "game_link.h"
#include "sandbox.h"

#include <lauxlib.h>
#include <lua.h>

#include <stdlib.h>
#include <string.h>

#define ITEM_MAX 99 /* Ov008_AddItemCount's cap */

static int done(lua_State *L, const char *why)
{
    if (why != NULL) {
        lua_pushnil(L);
        lua_pushstring(L, why);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* An item's count moved by `by`, kept within 0..99 (the owned flag set by the link when above 0). */
static int move_item(lua_State *L, int sign)
{
    unsigned char counts[KHDAYS_LINK_ITEMS];
    const lua_Integer id = luaL_checkinteger(L, 1), n = luaL_optinteger(L, 2, 1);
    lua_Integer count;
    if (id < 0 || id >= KHDAYS_LINK_ITEMS) {
        return done(L, "unknown item");
    }
    if (n < 0) {
        return luaL_argerror(L, 2, "a count of 0 or more");
    }
    if (!khdays_api_may_change(L, 1)) {
        return 2;
    }
    if (!khdays_link_items(counts)) {
        return done(L, "no game");
    }
    count = counts[id] + sign * (n > ITEM_MAX ? ITEM_MAX : n);
    khdays_link_set_item((int)id, (int)(count < 0 ? 0 : count > ITEM_MAX ? ITEM_MAX : count));
    return done(L, NULL);
}

static int give_item(lua_State *L)
{
    return move_item(L, 1);
}

static int take_item(lua_State *L)
{
    return move_item(L, -1);
}

/* A sound only: a visual mod and a guest may play one. */
static int play_sound(lua_State *L)
{
    return done(L, khdays_link_play_sound(luaL_checkstring(L, 1), luaL_checkstring(L, 2)));
}

/* UTF-8 into the game's UTF-16 units; 0 for a text that is not UTF-8, holds a control character or a
 * character beyond the first plane. */
static int utf16(const char *s, size_t n, unsigned short *out)
{
    int length = 0;
    for (size_t i = 0; i < n;) {
        const unsigned char c = (unsigned char)s[i];
        unsigned code, extra;
        if (c < 0x80) {
            code = c, extra = 0;
        } else if ((c & 0xe0) == 0xc0) {
            code = c & 0x1f, extra = 1;
        } else if ((c & 0xf0) == 0xe0) {
            code = c & 0x0f, extra = 2;
        } else {
            return 0;
        }
        if (extra > 0 && i + extra >= n) {
            return 0;
        }
        for (unsigned k = 1; k <= extra; ++k) {
            const unsigned char d = (unsigned char)s[i + k];
            if ((d & 0xc0) != 0x80) {
                return 0;
            }
            code = (code << 6) | (d & 0x3f);
        }
        if (code < 0x20 || (code >= 0x7f && code < 0xa0) || (code >= 0xd800 && code < 0xe000)) {
            return 0;
        }
        out[length++] = (unsigned short)code;
        i += extra + 1;
    }
    return length;
}

static int show_text(lua_State *L)
{
    size_t n;
    const char *text = luaL_checklstring(L, 1, &n);
    unsigned short *units;
    const char *why;
    int length;
    if (n == 0) {
        return luaL_argerror(L, 1, "an empty text");
    }
    if (!khdays_api_may_change(L, 0)) {
        return 2;
    }
    units = malloc(n * sizeof(*units));
    if (units == NULL) {
        return luaL_error(L, "not enough memory");
    }
    length = utf16(text, n, units);
    why = length > 0 ? khdays_link_show_text(units, length) : "not a text the game can show";
    free(units);
    return done(L, why);
}

static void add_to(lua_State *L, const char *table, const luaL_Reg *functions)
{
    lua_getglobal(L, table);
    luaL_setfuncs(L, functions, 0);
    lua_pop(L, 1);
}

void khdays_api_actions_open(KhdaysMod *m)
{
    static const luaL_Reg game[] = {{"give_item", give_item},
                                    {"take_item", take_item},
                                    {"play_sound", play_sound},
                                    {"show_text", show_text},
                                    {NULL, NULL}};
    add_to(m->L, "game", game);
}
