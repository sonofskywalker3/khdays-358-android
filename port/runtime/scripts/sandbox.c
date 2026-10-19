/* Each mod's own Lua state (sandbox.h). */
#include "sandbox.h"

#include "folders.h"
#include "mods_log.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 8 ms: measured, ten mods doing a mod's usual work took 45 us each a frame on average and 213 us at
 * most (port/tests/mods_load); a whole 1 ms was once passed by a pause of the thread itself. */
uint64_t khdays_sandbox_frame_allowance = 8000000u;
uint64_t khdays_sandbox_load_allowance = 250000000u; /* 250 ms */

#define HOOK_EVERY 10000 /* instructions between two looks at the time */

static uint64_t now_ns(void)
{
    return SDL_GetTicksNS();
}

KhdaysMod *khdays_sandbox_mod(lua_State *L)
{
    return *(KhdaysMod **)lua_getextraspace(L);
}

/* The mod's memory: what it holds counted, a request past its cap refused (Lua raises "not enough
 * memory", which the mod may catch: it goes on within its cap). */
static void *allocate(void *ud, void *block, size_t old, size_t size)
{
    KhdaysMod *m = ud;
    void *grown;
    if (block == NULL) {
        old = 0; /* `old` then tells the kind of object */
    }
    if (size == 0) {
        free(block);
        m->bytes -= old;
        return NULL;
    }
    if (size > old && m->bytes - old + size > KHDAYS_MOD_MEMORY) {
        return NULL;
    }
    grown = realloc(block, size);
    if (grown != NULL) {
        m->bytes = m->bytes - old + size;
    }
    return grown;
}

static int over_allowance(KhdaysMod *m)
{
    return m->over || (m->entered != 0 && m->spent + (now_ns() - m->entered) > m->allowance);
}

static int raise_over(lua_State *L)
{
    const KhdaysMod *m = khdays_sandbox_mod(L);
    char why[64]; /* lua_pushfstring has no %.1f */
    snprintf(why, sizeof(why), "over its time allowance (%.1f ms)", (double)m->allowance / 1e6);
    return luaL_error(L, "%s", why);
}

static void count_hook(lua_State *L, lua_Debug *ar)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    (void)ar;
    if (over_allowance(m)) {
        m->over = 1;
        raise_over(L);
    }
}

/* pcall, xpcall and coroutine.resume as Lua's, except that a mod past its allowance cannot catch it:
 * the error goes on up to the port's own call. */
static int after_protected(lua_State *L, int status, lua_KContext context)
{
    (void)status;
    (void)context;
    if (khdays_sandbox_mod(L)->over) {
        return raise_over(L);
    }
    return lua_gettop(L);
}

static int protected_call(lua_State *L)
{
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_callk(L, lua_gettop(L) - 1, LUA_MULTRET, 0, after_protected);
    return after_protected(L, LUA_OK, 0);
}

/* setmetatable without __gc: finalizers run with the time hook off, so one that never ends would stop
 * the game. */
static int setmetatable_no_gc(lua_State *L)
{
    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_pushliteral(L, "__gc");
        if (lua_rawget(L, 2) != LUA_TNIL) {
            return luaL_error(L, "__gc metamethods are not available to mods");
        }
        lua_pop(L, 1);
    }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, 1);
    return 1;
}

/* load of text only: precompiled chunks are refused. */
static int load_text(lua_State *L)
{
    const int n = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 1);
    if (n >= 2) {
        lua_pushvalue(L, 2);
    } else {
        lua_pushnil(L);
    }
    lua_pushliteral(L, "t");
    if (n >= 4) {
        lua_pushvalue(L, 4);
    }
    lua_call(L, n >= 4 ? 4 : 3, LUA_MULTRET);
    return lua_gettop(L) - n;
}

void khdays_sandbox_where(lua_State *L, int level, char *file, int size, int *line)
{
    lua_Debug ar;
    file[0] = 0;
    *line = 0;
    for (; lua_getstack(L, level, &ar); ++level) {
        if (lua_getinfo(L, "Sl", &ar) && ar.currentline > 0) {
            snprintf(file, (size_t)size, "%s", ar.short_src);
            *line = ar.currentline;
            return;
        }
    }
}

static int print_to_log(lua_State *L)
{
    const int n = lua_gettop(L);
    const KhdaysMod *m = khdays_sandbox_mod(L);
    char file[256];
    int line;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; ++i) {
        luaL_tolstring(L, i, NULL);
        luaL_addvalue(&b);
        if (i < n) {
            luaL_addchar(&b, '\t');
        }
    }
    luaL_pushresult(&b);
    khdays_sandbox_where(L, 1, file, sizeof(file), &line);
    khdays_mods_log(m->manifest.id, "print", file[0] ? file : NULL, line, lua_tostring(L, -1));
    return 0;
}

int khdays_sandbox_path_ok(const char *path)
{
    if (path == NULL || *path == 0 || *path == '/' || strchr(path, '\\') != NULL || strchr(path, ':') != NULL) {
        return 0;
    }
    for (const char *part = path; *part;) {
        const char *end = strchr(part, '/');
        const size_t n = end != NULL ? (size_t)(end - part) : strlen(part);
        if (n == 0 || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) {
            return 0;
        }
        part += n;
        if (*part == '/') {
            ++part;
            if (*part == 0) {
                return 0;
            }
        }
    }
    return 1;
}

/* require("a.b"): the mod's scripts/a/b.lua, run once, its result kept. Names are letters, digits,
 * '_' and '-' between dots: nothing outside scripts/ can be named. */
static int require_script(lua_State *L)
{
    const KhdaysMod *m = khdays_sandbox_mod(L);
    const char *name = luaL_checkstring(L, 1);
    size_t size = 0;
    char *path, *text, chunk[512];
    int ok = *name != 0 && name[strlen(name) - 1] != '.';
    for (const char *p = name; ok && *p; ++p) {
        const char c = *p;
        ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
             (c == '.' && p != name && p[-1] != '.');
    }
    if (!ok) {
        return luaL_error(L, "require: '%s' is not a script name of the mod", name);
    }
    lua_getfield(L, LUA_REGISTRYINDEX, "khdays.required");
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1)) {
        return 1;
    }
    lua_pop(L, 1);
    snprintf(chunk, sizeof(chunk), "@scripts/%s.lua", name);
    for (char *p = chunk + 9; *p; ++p) {
        if (*p == '.' && strcmp(p, ".lua") != 0) {
            *p = '/';
        }
    }
    size = strlen(m->path) + strlen(chunk) + 2;
    path = malloc(size);
    if (path == NULL) {
        return luaL_error(L, "not enough memory");
    }
    snprintf(path, size, "%s/%s", m->path, chunk + 1);
    text = khdays_folders_read(path, &size);
    free(path);
    if (text == NULL) {
        return luaL_error(L, "require: %s not found", chunk + 1);
    }
    ok = luaL_loadbufferx(L, text, size, chunk, "t");
    free(text);
    if (ok != LUA_OK) {
        return lua_error(L);
    }
    lua_pushstring(L, name);
    lua_call(L, 1, 1);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name);
    return 1;
}

/* Replaces table[name] (the table at `t`) by `f` holding the original as its upvalue. */
static void wrap(lua_State *L, int t, const char *name, lua_CFunction f)
{
    t = lua_absindex(L, t);
    lua_getfield(L, t, name);
    lua_pushcclosure(L, f, 1);
    lua_setfield(L, t, name);
}

int khdays_sandbox_open(KhdaysMod *m)
{
    static const luaL_Reg libs[] = {{LUA_GNAME, luaopen_base},
                                    {LUA_STRLIBNAME, luaopen_string},
                                    {LUA_TABLIBNAME, luaopen_table},
                                    {LUA_MATHLIBNAME, luaopen_math},
                                    {LUA_UTF8LIBNAME, luaopen_utf8},
                                    {LUA_COLIBNAME, luaopen_coroutine},
                                    {NULL, NULL}};
    static const char *const os_kept[] = {"time", "clock", "date", "difftime"};
    lua_State *L;
    m->bytes = 0;
    L = lua_newstate(allocate, m);
    if (L == NULL) {
        khdays_mod_set_state(m, KHDAYS_MOD_REFUSED, "not enough memory for its interpreter");
        return 0;
    }
    *(KhdaysMod **)lua_getextraspace(L) = m;
    m->L = L;
    m->over = 0;
    m->entered = 0;
    for (const luaL_Reg *lib = libs; lib->func != NULL; ++lib) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    /* os: its clock and dates only */
    luaL_requiref(L, LUA_OSLIBNAME, luaopen_os, 0);
    lua_createtable(L, 0, 4);
    for (size_t i = 0; i < sizeof(os_kept) / sizeof(os_kept[0]); ++i) {
        lua_getfield(L, -2, os_kept[i]);
        lua_setfield(L, -2, os_kept[i]);
    }
    lua_setglobal(L, LUA_OSLIBNAME);
    lua_pop(L, 1);

    lua_pushglobaltable(L);
    lua_pushnil(L);
    lua_setfield(L, -2, "dofile");
    lua_pushnil(L);
    lua_setfield(L, -2, "loadfile");
    lua_pushcfunction(L, print_to_log);
    lua_setfield(L, -2, "print");
    lua_pushcfunction(L, require_script);
    lua_setfield(L, -2, "require");
    wrap(L, -1, "load", load_text);
    wrap(L, -1, "pcall", protected_call);
    wrap(L, -1, "xpcall", protected_call);
    wrap(L, -1, "setmetatable", setmetatable_no_gc);
    lua_getfield(L, -1, LUA_COLIBNAME);
    wrap(L, -1, "resume", protected_call);
    lua_pop(L, 2);

    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "khdays.required");
    lua_sethook(L, count_hook, LUA_MASKCOUNT, HOOK_EVERY);
    return 1;
}

void khdays_sandbox_close(KhdaysMod *m)
{
    if (m->L != NULL) {
        lua_close(m->L);
        m->L = NULL;
    }
}

static int add_traceback(lua_State *L)
{
    const char *message = luaL_tolstring(L, 1, NULL);
    luaL_traceback(L, L, message, 1);
    return 1;
}

/* An error's first line "file:line: text" split; `text` points into `message`. */
static void split_error(const char *message, char *file, int size, int *line, const char **text)
{
    const char *eol = strchr(message, '\n');
    const char *end = eol != NULL ? eol : message + strlen(message);
    *line = 0;
    file[0] = 0;
    *text = message;
    for (const char *c = message; c < end; ++c) {
        const char *d = c + 1;
        int n = 0;
        if (*c != ':') {
            continue;
        }
        while (d < end && *d >= '0' && *d <= '9') {
            n = n * 10 + (*d++ - '0');
        }
        if (d > c + 1 && d < end && *d == ':') {
            snprintf(file, (size_t)size, "%.*s", (int)(c - message), message);
            *line = n;
            *text = d[1] == ' ' ? d + 2 : d + 1;
            return;
        }
    }
}

/* What a failed call leaves: the error logged, the mod stopped or (past its allowance) paused. */
static void failed(KhdaysMod *m, const char *message)
{
    char file[256], reason[300];
    const char *text;
    int line;
    split_error(message, file, sizeof(file), &line, &text);
    khdays_mods_log(m->manifest.id, "error", file[0] ? file : NULL, line, text);
    if (m->over) {
        snprintf(reason, sizeof(reason), "over its time allowance (%.1f ms)", (double)m->allowance / 1e6);
        khdays_mod_set_state(m, KHDAYS_MOD_PAUSED, reason);
        khdays_mods_log(m->manifest.id, "paused", NULL, 0, reason);
    } else {
        const char *eol = strchr(text, '\n');
        const int n = (int)(eol != NULL ? eol - text : (ptrdiff_t)strlen(text));
        if (file[0] != 0) {
            snprintf(reason, sizeof(reason), "%s:%d: %.*s", file, line, n, text);
        } else {
            snprintf(reason, sizeof(reason), "%.*s", n, text);
        }
        khdays_mod_set_state(m, KHDAYS_MOD_STOPPED, reason);
        khdays_mods_log(m->manifest.id, "stopped", NULL, 0, "the error above");
    }
}

int khdays_sandbox_call(KhdaysMod *m, int nargs, int nresults)
{
    lua_State *L = m->L;
    const int outer = m->entered == 0;
    const int base = lua_gettop(L) - nargs;
    int status;
    if (m->state != KHDAYS_MOD_LOADED) {
        lua_settop(L, base - 1);
        return 0;
    }
    lua_pushcfunction(L, add_traceback);
    lua_insert(L, base);
    if (outer) {
        m->entered = now_ns();
    }
    status = lua_pcall(L, nargs, nresults, base);
    if (outer) {
        m->spent += now_ns() - m->entered;
        m->entered = 0;
    }
    lua_remove(L, base);
    if (status != LUA_OK) {
        failed(m, lua_tostring(L, -1) != NULL ? lua_tostring(L, -1) : "(an error without a message)");
        lua_settop(L, base - 1);
        return 0;
    }
    return 1;
}

int khdays_sandbox_run_main(KhdaysMod *m)
{
    lua_State *L = m->L;
    size_t size = 0, n = strlen(m->path) + strlen(m->manifest.main) + 2;
    char *path = malloc(n), *text, chunk[512];
    uint64_t frame_allowance;
    int ok;
    if (path == NULL) {
        khdays_mod_set_state(m, KHDAYS_MOD_REFUSED, "not enough memory");
        return 0;
    }
    snprintf(path, n, "%s/%s", m->path, m->manifest.main);
    text = khdays_folders_read(path, &size);
    free(path);
    if (text == NULL) {
        char reason[300];
        snprintf(reason, sizeof(reason), "main script %s not found", m->manifest.main);
        khdays_mod_set_state(m, KHDAYS_MOD_REFUSED, reason);
        khdays_mods_log(m->manifest.id, "refused", NULL, 0, reason);
        return 0;
    }
    snprintf(chunk, sizeof(chunk), "@%s", m->manifest.main);
    if (luaL_loadbufferx(L, text, size, chunk, "t") != LUA_OK) {
        free(text);
        khdays_mod_set_state(m, KHDAYS_MOD_LOADED, NULL);
        failed(m, lua_tostring(L, -1));
        lua_pop(L, 1);
        return 0;
    }
    free(text);
    khdays_mod_set_state(m, KHDAYS_MOD_LOADED, NULL);
    frame_allowance = m->allowance;
    m->allowance = khdays_sandbox_load_allowance;
    m->spent = 0;
    ok = khdays_sandbox_call(m, 0, 0);
    m->allowance = frame_allowance;
    m->spent = 0;
    return ok;
}
