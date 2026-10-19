/* The mod table: the mod's own names and its subscriptions to the game's events. */
#include "api.h"

#include "events.h"
#include "folders.h"
#include "game_link.h"
#include "sandbox.h"
#include "view.h"

#include <lauxlib.h>
#include <lua.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int frame(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)khdays_link_frame());
    return 1;
}

/* mod.notice(text): a line the player sees over the game, with the mod's name. */
static int notice(lua_State *L)
{
    const KhdaysMod *m = khdays_sandbox_mod(L);
    khdays_scripts_notice(KHDAYS_NOTICE_SAID, m->manifest.name, luaL_checkstring(L, 1));
    return 0;
}

static char *joined(const char *a, const char *b)
{
    const size_t n = strlen(a) + strlen(b) + 2;
    char *out = malloc(n);
    if (out != NULL) {
        snprintf(out, n, "%s/%s", a, b);
    }
    return out;
}

/* mod.read(path): a text the mod shipped (its data/) or wrote (its folder in the player's saves). */
static int read_file(lua_State *L)
{
    const KhdaysMod *m = khdays_sandbox_mod(L);
    const char *path = luaL_checkstring(L, 1);
    char *shipped, *written, *text = NULL;
    size_t size = 0;
    if (!khdays_sandbox_path_ok(path)) {
        lua_pushnil(L);
        lua_pushliteral(L, "not a path of the mod's");
        return 2;
    }
    shipped = joined(m->path, "data");
    written = m->data != NULL ? joined(m->data, path) : NULL;
    if (shipped != NULL) {
        char *file = joined(shipped, path);
        text = file != NULL ? khdays_folders_read(file, &size) : NULL;
        free(file);
    }
    if (text == NULL && written != NULL) {
        text = khdays_folders_read(written, &size);
    }
    free(shipped);
    free(written);
    if (text == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "not found");
        return 2;
    }
    lua_pushlstring(L, text, size);
    free(text);
    return 1;
}

/* mod.write(path, text): into the mod's folder in the player's saves, its folders made. */
static int write_file(lua_State *L)
{
    const KhdaysMod *m = khdays_sandbox_mod(L);
    const char *path = luaL_checkstring(L, 1);
    size_t size;
    const char *text = luaL_checklstring(L, 2, &size);
    char *file;
    int ok;
    if (!khdays_sandbox_path_ok(path)) {
        lua_pushnil(L);
        lua_pushliteral(L, "not a path of the mod's");
        return 2;
    }
    file = m->data != NULL ? joined(m->data, path) : NULL;
    ok = file != NULL && khdays_folders_write(file, text, size);
    free(file);
    if (!ok) {
        lua_pushnil(L);
        lua_pushliteral(L, "cannot be written");
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

void khdays_api_mod_open(KhdaysMod *m)
{
    static const luaL_Reg functions[] = {{"on", khdays_script_events_on},
                                         {"off", khdays_script_events_off},
                                         {"frame", frame},
                                         {"notice", notice},
                                         {"read", read_file},
                                         {"write", write_file},
                                         {NULL, NULL}};
    lua_State *L = m->L;
    luaL_newlib(L, functions);
    lua_pushstring(L, m->manifest.id);
    lua_setfield(L, -2, "id");
    lua_pushstring(L, m->manifest.name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, m->manifest.version);
    lua_setfield(L, -2, "version");
    lua_setglobal(L, "mod");
}
