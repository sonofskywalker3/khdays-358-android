/* mod.option: the options a mod adds to the settings menu's Mods page, their values kept in the
 * player's saves folder (mods/<id>/options.ini) and their code run on the game's thread when the
 * player changes them (the window's thread asks through queue.h). */
#include "api.h"

#include "events.h"
#include "folders.h"
#include "sandbox.h"

#include <lauxlib.h>
#include <lua.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const KINDS[] = {"toggle", "choice", "number", "button"};
static const char *const LANGUAGES[KHDAYS_OPTION_LANGUAGES] = {"", "en", "fr", "de", "it", "es"};

static char *copy(const char *s)
{
    char *out = malloc(strlen(s) + 1);
    if (out != NULL) {
        strcpy(out, s);
    }
    return out;
}

static void free_option(KhdaysOption *o)
{
    free(o->key);
    for (int i = 0; i < KHDAYS_OPTION_LANGUAGES; ++i) {
        free(o->label[i]);
        free(o->description[i]);
    }
    for (int i = 0; i < o->value_count; ++i) {
        free(o->values[i]);
    }
    free(o->values);
    memset(o, 0, sizeof(*o));
}

void khdays_api_menus_free(KhdaysMod *m)
{
    for (int i = 0; i < m->option_count; ++i) {
        free_option(&m->options[i]);
    }
    free(m->options);
    m->options = NULL;
    m->option_count = m->option_capacity = 0;
}

/* A label or description: one text for every language, or a table by language code. */
static int read_texts(lua_State *L, int t, const char *field, char *out[KHDAYS_OPTION_LANGUAGES], int required)
{
    int any = 0;
    lua_getfield(L, t, field);
    if (lua_type(L, -1) == LUA_TSTRING) {
        out[0] = copy(lua_tostring(L, -1));
        any = out[0] != NULL;
    } else if (lua_type(L, -1) == LUA_TTABLE) {
        for (int i = 1; i < KHDAYS_OPTION_LANGUAGES; ++i) {
            lua_getfield(L, -1, LANGUAGES[i]);
            if (lua_type(L, -1) == LUA_TSTRING) {
                out[i] = copy(lua_tostring(L, -1));
                any = any || out[i] != NULL;
            }
            lua_pop(L, 1);
        }
    } else if (!lua_isnil(L, -1)) {
        return luaL_error(L, "mod.option: %s must be a text or a table of texts by language", field);
    }
    lua_pop(L, 1);
    if (required && !any) {
        return luaL_error(L, "mod.option: %s is required", field);
    }
    return 0;
}

static double number_field(lua_State *L, int t, const char *field, double fallback)
{
    double v = fallback;
    lua_getfield(L, t, field);
    if (!lua_isnil(L, -1)) {
        if (!lua_isnumber(L, -1)) {
            luaL_error(L, "mod.option: %s must be a number", field);
        }
        v = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);
    return v;
}

/* A value the option can hold: toggles 0 or 1, a choice's index within its values, a number within
 * its range on its steps. Returns 0 when it cannot. */
static int fit(const KhdaysOption *o, double v, double *out)
{
    if (!isfinite(v)) {
        return 0;
    }
    switch (o->kind) {
    case KHDAYS_OPTION_TOGGLE: *out = v != 0 ? 1 : 0; return 1;
    case KHDAYS_OPTION_CHOICE:
        if (v != floor(v) || v < 1 || v > o->value_count) {
            return 0;
        }
        *out = v;
        return 1;
    case KHDAYS_OPTION_NUMBER:
        if (v < o->low || v > o->high) {
            return 0;
        }
        *out = o->low + floor((v - o->low) / o->step + 0.5) * o->step;
        if (*out > o->high) {
            *out = o->high;
        }
        return 1;
    default: return 0;
    }
}

static char *options_path(const KhdaysMod *m)
{
    const size_t n = strlen(m->data) + 13;
    char *path = malloc(n);
    if (path != NULL) {
        snprintf(path, n, "%s/options.ini", m->data);
    }
    return path;
}

/* The value kept for `key` in options.ini, if there is one. */
static int kept_value(const KhdaysMod *m, const char *key, double *out)
{
    char *path = options_path(m), *text;
    const size_t n = strlen(key);
    int found = 0;
    text = path != NULL ? khdays_folders_read(path, NULL) : NULL;
    free(path);
    for (const char *line = text; line != NULL && *line && !found;) {
        const char *eol = strchr(line, '\n');
        while (*line == ' ' || *line == '\t') {
            ++line;
        }
        if (strncmp(line, key, n) == 0) {
            const char *p = line + n;
            while (*p == ' ' || *p == '\t') {
                ++p;
            }
            if (*p == '=') {
                char *end;
                *out = strtod(p + 1, &end);
                found = end != p + 1;
            }
        }
        line = eol != NULL ? eol + 1 : NULL;
    }
    free(text);
    return found;
}

static void save_values(const KhdaysMod *m)
{
    size_t size = 1, at = 0;
    char *text, *path;
    for (int i = 0; i < m->option_count; ++i) {
        size += strlen(m->options[i].key) + 40;
    }
    text = malloc(size);
    path = options_path(m);
    if (text == NULL || path == NULL) {
        free(text);
        free(path);
        return;
    }
    for (int i = 0; i < m->option_count; ++i) {
        const KhdaysOption *o = &m->options[i];
        if (o->kind != KHDAYS_OPTION_BUTTON) {
            at += (size_t)snprintf(text + at, size - at, "%s = %.17g\n", o->key, o->value);
        }
    }
    khdays_folders_write(path, text, at);
    free(text);
    free(path);
}

static void push_value(lua_State *L, const KhdaysOption *o)
{
    switch (o->kind) {
    case KHDAYS_OPTION_TOGGLE: lua_pushboolean(L, o->value != 0); break;
    case KHDAYS_OPTION_CHOICE: lua_pushinteger(L, (lua_Integer)o->value); break;
    case KHDAYS_OPTION_NUMBER: lua_pushnumber(L, o->value); break;
    default: lua_pushnil(L); break;
    }
}

static KhdaysOption *option_of(lua_State *L)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    const int index = (int)lua_tointeger(L, lua_upvalueindex(1));
    return index >= 0 && index < m->option_count ? &m->options[index] : NULL;
}

/* option.value */
static int option_index(lua_State *L)
{
    const KhdaysOption *o = option_of(L);
    if (o != NULL && strcmp(luaL_checkstring(L, 2), "value") == 0) {
        push_value(L, o);
        return 1;
    }
    return 0;
}

/* option:set(v): the mod's own change, kept like the player's (its on_change is not called). */
static int option_set(lua_State *L)
{
    KhdaysOption *o = option_of(L);
    double v;
    if (o == NULL || o->kind == KHDAYS_OPTION_BUTTON) {
        return luaL_error(L, "option:set: a button has no value");
    }
    v = lua_isboolean(L, 2) ? (double)lua_toboolean(L, 2) : luaL_checknumber(L, 2);
    if (!fit(o, v, &v)) {
        lua_pushnil(L);
        lua_pushliteral(L, "not a value of the option");
        return 2;
    }
    o->value = v;
    save_values(khdays_sandbox_mod(L));
    lua_pushboolean(L, 1);
    return 1;
}

static int add_option(lua_State *L)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    KhdaysOption o;
    const char *key, *kind;
    int index;
    luaL_checktype(L, 1, LUA_TTABLE);
    memset(&o, 0, sizeof(o));
    o.ref = LUA_NOREF;
    lua_getfield(L, 1, "key");
    key = lua_tostring(L, -1);
    if (key == NULL || *key == 0 || strpbrk(key, "=\n\r") != NULL) {
        return luaL_error(L, "mod.option: key must be a text without '=' or line breaks");
    }
    for (int i = 0; i < m->option_count; ++i) {
        if (strcmp(m->options[i].key, key) == 0) {
            return luaL_error(L, "mod.option: the key '%s' is taken", key);
        }
    }
    lua_getfield(L, 1, "kind");
    kind = lua_tostring(L, -1);
    o.kind = -1;
    for (int i = 0; kind != NULL && i < 4; ++i) {
        if (strcmp(kind, KINDS[i]) == 0) {
            o.kind = i;
        }
    }
    if (o.kind < 0) {
        return luaL_error(L, "mod.option: kind must be toggle, choice, number or button");
    }
    lua_pop(L, 2);
    if (o.kind == KHDAYS_OPTION_CHOICE) {
        lua_getfield(L, 1, "values");
        if (!lua_istable(L, -1) || luaL_len(L, -1) < 2) {
            return luaL_error(L, "mod.option: a choice needs at least 2 values");
        }
        o.value_count = (int)luaL_len(L, -1);
        o.values = calloc((size_t)o.value_count, sizeof(*o.values));
        for (int i = 0; o.values != NULL && i < o.value_count; ++i) {
            lua_geti(L, -1, i + 1);
            o.values[i] = copy(luaL_tolstring(L, -1, NULL));
            lua_pop(L, 2);
        }
        lua_pop(L, 1);
    }
    if (o.kind == KHDAYS_OPTION_NUMBER) {
        o.low = number_field(L, 1, "min", 0);
        o.high = number_field(L, 1, "max", 0);
        o.step = number_field(L, 1, "step", 1);
        if (!(o.low < o.high) || !(o.step > 0) || o.low != floor(o.low) || o.high != floor(o.high) ||
            o.step != floor(o.step) || fabs(o.low) > 1e9 || fabs(o.high) > 1e9) {
            free_option(&o);
            return luaL_error(L, "mod.option: a number needs whole min < max and a whole step > 0");
        }
    }
    read_texts(L, 1, "label", o.label, 1);
    read_texts(L, 1, "description", o.description, 0);
    lua_getfield(L, 1, "default");
    o.fallback = o.kind == KHDAYS_OPTION_CHOICE ? 1 : o.kind == KHDAYS_OPTION_NUMBER ? o.low : 0;
    if (!lua_isnil(L, -1)) {
        const double v = lua_isboolean(L, -1) ? (double)lua_toboolean(L, -1) : lua_tonumber(L, -1);
        if (!fit(&o, v, &o.fallback)) {
            free_option(&o);
            return luaL_error(L, "mod.option: default is not a value of the option");
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "on_change");
    if (lua_isfunction(L, -1)) {
        o.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }
    o.key = copy(key);
    {
        double kept;
        o.value = kept_value(m, key, &kept) && fit(&o, kept, &kept) ? kept : o.fallback;
    }
    if (m->option_count == m->option_capacity) {
        const int grown = m->option_capacity == 0 ? 8 : m->option_capacity * 2;
        KhdaysOption *more = realloc(m->options, (size_t)grown * sizeof(*more));
        if (more == NULL) {
            free_option(&o);
            return luaL_error(L, "not enough memory");
        }
        m->options = more;
        m->option_capacity = grown;
    }
    index = m->option_count;
    m->options[m->option_count++] = o;

    /* the object: { key, set }, its value read through __index */
    lua_createtable(L, 0, 2);
    lua_pushstring(L, key);
    lua_setfield(L, -2, "key");
    lua_pushinteger(L, index);
    lua_pushcclosure(L, option_set, 1);
    lua_setfield(L, -2, "set");
    lua_createtable(L, 0, 1);
    lua_pushinteger(L, index);
    lua_pushcclosure(L, option_index, 1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    return 1;
}

void khdays_api_menus_open(KhdaysMod *m)
{
    lua_State *L = m->L;
    lua_getglobal(L, "mod");
    lua_pushcfunction(L, add_option);
    lua_setfield(L, -2, "option");
    lua_pop(L, 1);
}

/* The player changed an option (value) or pressed a button: kept, then its code run. */
void khdays_api_menus_use(KhdaysMod *m, const char *key, double value)
{
    for (int i = 0; i < m->option_count; ++i) {
        KhdaysOption *o = &m->options[i];
        double v = value;
        if (strcmp(o->key, key) != 0) {
            continue;
        }
        if (o->kind != KHDAYS_OPTION_BUTTON) {
            if (!fit(o, value, &v)) {
                return;
            }
            o->value = v;
            save_values(m);
        }
        if (o->ref != LUA_NOREF && m->state == KHDAYS_MOD_LOADED) {
            const int button = o->kind == KHDAYS_OPTION_BUTTON;
            lua_rawgeti(m->L, LUA_REGISTRYINDEX, o->ref);
            if (!button) {
                push_value(m->L, o);
            }
            khdays_script_events_hold();
            khdays_sandbox_call(m, button ? 0 : 1, 0);
            khdays_script_events_release();
        }
        return;
    }
}
