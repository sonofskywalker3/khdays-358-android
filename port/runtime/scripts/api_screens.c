/* A mod's title entries and screens (api_v2.h; screens.h shares them with the window that draws them).
 * A label or text is one text or a table of texts by language, taken in the game's language when the
 * screen shows. One screen shows at a time, whichever mod made it; any answer closes it, and the mod's
 * function hears it at the next frame point. */
#include "api_v2.h"

#include "events.h"
#include "sandbox.h"
#include "screens.h"

#include <lauxlib.h>
#include <lua.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int next_owner, next_handle;

static char *copy(const char *s)
{
    char *out = malloc(strlen(s) + 1);
    if (out != NULL) {
        strcpy(out, s);
    }
    return out;
}

/* The text at `index` (a text, or a table of texts by language code) in the game's language; NULL for
 * nil. */
static char *text_at(lua_State *L, int index, const char *what)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    const int language = khdays_link_language();
    if (lua_isnoneornil(L, index)) {
        return NULL;
    }
    if (lua_type(L, index) == LUA_TSTRING || lua_type(L, index) == LUA_TNUMBER) {
        return copy(lua_tostring(L, index));
    }
    if (lua_type(L, index) != LUA_TTABLE) {
        luaL_error(L, "%s must be a text or a table of texts by language", what);
        return NULL;
    }
    for (int pass = 0; pass < 6; ++pass) {
        const char *code = pass == 0 ? (language >= 1 && language <= 5 ? codes[language] : "en") : codes[pass];
        if (pass > 0 && code[0] == '\0') {
            continue;
        }
        lua_getfield(L, index, code);
        if (lua_type(L, -1) == LUA_TSTRING) {
            char *out = copy(lua_tostring(L, -1));
            lua_pop(L, 1);
            return out;
        }
        lua_pop(L, 1);
    }
    return copy("");
}

static char *field_text(lua_State *L, const char *field, const char *what)
{
    char *out;
    lua_getfield(L, 1, field);
    out = text_at(L, lua_gettop(L), what);
    lua_pop(L, 1);
    return out;
}

static int field_function(lua_State *L, const char *field)
{
    lua_getfield(L, 1, field);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return LUA_NOREF;
    }
    if (!lua_isfunction(L, -1)) {
        return luaL_error(L, "%s must be a function", field);
    }
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

static KhdaysModCallbacks *add_callbacks(KhdaysMod *m)
{
    KhdaysModV2 *v = m->v2;
    if (v->callback_count == v->callback_room) {
        const int room = v->callback_room ? v->callback_room * 2 : 8;
        KhdaysModCallbacks *grown = realloc(v->callbacks, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        v->callbacks = grown;
        v->callback_room = room;
    }
    memset(&v->callbacks[v->callback_count], 0, sizeof(*v->callbacks));
    v->callbacks[v->callback_count].handle = ++next_handle;
    v->callbacks[v->callback_count].on_choose = v->callbacks[v->callback_count].on_cancel = LUA_NOREF;
    v->callbacks[v->callback_count].on_close = v->callbacks[v->callback_count].on_select = LUA_NOREF;
    return &v->callbacks[v->callback_count++];
}

static void drop_callbacks(KhdaysMod *m, int handle)
{
    KhdaysModV2 *v = m->v2;
    for (int i = 0; i < v->callback_count; ++i) {
        if (v->callbacks[i].handle == handle) {
            const int refs[4] = {v->callbacks[i].on_choose, v->callbacks[i].on_cancel, v->callbacks[i].on_close,
                                 v->callbacks[i].on_select};
            for (int r = 0; r < 4; ++r) {
                if (refs[r] != LUA_NOREF && m->L != NULL) {
                    luaL_unref(m->L, LUA_REGISTRYINDEX, refs[r]);
                }
            }
            v->callbacks[i] = v->callbacks[--v->callback_count];
            return;
        }
    }
}

/* title.entry{label, on_select} */
static int entry(lua_State *L)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    KhdaysMod *m = khdays_sandbox_mod(L);
    char *labels[6] = {0};
    KhdaysModCallbacks *c;
    int any = 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!m->v2->loading) {
        lua_pushnil(L);
        lua_pushliteral(L, "only while loading");
        return 2;
    }
    lua_getfield(L, 1, "label");
    if (lua_type(L, -1) == LUA_TSTRING) {
        labels[0] = copy(lua_tostring(L, -1));
        any = 1;
    } else if (lua_type(L, -1) == LUA_TTABLE) {
        for (int i = 1; i < 6; ++i) {
            lua_getfield(L, -1, codes[i]);
            if (lua_type(L, -1) == LUA_TSTRING) {
                labels[i] = copy(lua_tostring(L, -1));
                any = 1;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    if (!any) {
        return luaL_error(L, "title.entry: label must be a text or a table of texts by language");
    }
    c = add_callbacks(m);
    if (c == NULL) {
        return luaL_error(L, "not enough memory");
    }
    c->on_select = field_function(L, "on_select");
    khdays_screens_add_entry(m->v2->owner, c->handle, labels);
    for (int i = 0; i < 6; ++i) {
        free(labels[i]);
    }
    lua_pushinteger(L, c->handle);
    return 1;
}

/* The rows of a list (texts) or of a result ({label, value} pairs, or tables with them). */
static int read_rows(lua_State *L, const char *field, int pairs, KhdaysScreen *s)
{
    lua_Integer n;
    lua_getfield(L, 1, field);
    if (!lua_istable(L, -1)) {
        return luaL_error(L, "%s must be a list", field);
    }
    n = luaL_len(L, -1);
    s->rows = n > 0 ? malloc((size_t)n * sizeof(*s->rows)) : NULL;
    for (lua_Integer i = 0; s->rows != NULL && i < n; ++i) {
        s->rows[i].label = s->rows[i].value = NULL;
    }
    for (lua_Integer i = 1; s->rows != NULL && i <= n; ++i) {
        lua_rawgeti(L, -1, i);
        if (pairs) {
            if (!lua_istable(L, -1)) {
                return luaL_error(L, "%s: each row must be {label, value}", field);
            }
            lua_rawgeti(L, -1, 1);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_getfield(L, -1, "label");
            }
            s->rows[i - 1].label = text_at(L, lua_gettop(L), "a row's label");
            lua_pop(L, 1);
            lua_rawgeti(L, -1, 2);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_getfield(L, -1, "value");
            }
            s->rows[i - 1].value = text_at(L, lua_gettop(L), "a row's value");
            lua_pop(L, 1);
        } else {
            s->rows[i - 1].label = text_at(L, lua_gettop(L), "an entry");
        }
        if (s->rows[i - 1].label == NULL) {
            s->rows[i - 1].label = copy("");
        }
        lua_pop(L, 1);
        s->row_count = (int)i;
    }
    lua_pop(L, 1);
    return 0;
}

/* A path of the mod's folder (its layouts/<name>.txt for a layout), NULL when not one. */
static char *mod_path(const KhdaysMod *m, const char *relative, const char *prefix, const char *suffix)
{
    char *out;
    size_t n;
    if (relative == NULL || !khdays_sandbox_path_ok(relative)) {
        return NULL;
    }
    n = strlen(m->path) + strlen(prefix) + strlen(relative) + strlen(suffix) + 2;
    out = malloc(n);
    if (out != NULL) {
        snprintf(out, n, "%s/%s%s%s", m->path, prefix, relative, suffix);
    }
    return out;
}

static void free_screen(KhdaysScreen *s)
{
    free(s->title);
    free(s->text);
    free(s->picture);
    free(s->layout);
    for (int i = 0; i < s->row_count; ++i) {
#pragma warning(suppress : 6001) /* every row below the count was written before the count moved */
        free(s->rows[i].label);
#pragma warning(suppress : 6001)
        free(s->rows[i].value);
    }
    free(s->rows);
}

/* screen.list / page / result */
static int show(lua_State *L, int kind)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    KhdaysScreen s;
    KhdaysModCallbacks *c;
    luaL_checktype(L, 1, LUA_TTABLE);
    if (khdays_screens_shown() != 0) {
        lua_pushnil(L);
        lua_pushliteral(L, "another screen shows");
        return 2;
    }
    memset(&s, 0, sizeof(s));
    s.kind = kind;
    s.title = field_text(L, "title", "title");
    if (kind == KHDAYS_SCREEN_LIST) {
        read_rows(L, "entries", 0, &s);
    } else if (kind == KHDAYS_SCREEN_RESULT) {
        read_rows(L, "rows", 1, &s);
    } else {
        s.text = field_text(L, "text", "text");
        lua_getfield(L, 1, "picture");
        if (!lua_isnil(L, -1)) {
            s.picture = mod_path(m, luaL_checkstring(L, -1), "", "");
            if (s.picture == NULL) {
                free_screen(&s);
                return luaL_error(L, "screen.page: picture must be a path of the mod's");
            }
        }
        lua_pop(L, 1);
    }
    lua_getfield(L, 1, "layout");
    if (!lua_isnil(L, -1)) {
        s.layout = mod_path(m, luaL_checkstring(L, -1), "layouts/", ".txt");
        if (s.layout == NULL) {
            free_screen(&s);
            return luaL_error(L, "layout must be a layout's name of the mod's");
        }
    }
    lua_pop(L, 1);
    c = add_callbacks(m);
    if (c == NULL) {
        free_screen(&s);
        return luaL_error(L, "not enough memory");
    }
    c->on_choose = field_function(L, "on_choose");
    c->on_cancel = field_function(L, "on_cancel");
    c->on_close = field_function(L, "on_close");
    khdays_screens_show(&s, m->v2->owner, c->handle);
    free_screen(&s);
    lua_pushinteger(L, c->handle);
    return 1;
}

static int list(lua_State *L)
{
    return show(L, KHDAYS_SCREEN_LIST);
}

static int page(lua_State *L)
{
    return show(L, KHDAYS_SCREEN_PAGE);
}

static int result(lua_State *L)
{
    return show(L, KHDAYS_SCREEN_RESULT);
}

/* screen.close(h): the screen goes without an answer. */
static int close_screen(lua_State *L)
{
    KhdaysMod *m = khdays_sandbox_mod(L);
    const int handle = (int)luaL_checkinteger(L, 1);
    if (khdays_screens_shown() != handle) {
        lua_pushnil(L);
        lua_pushliteral(L, "not shown");
        return 2;
    }
    khdays_screens_close(handle);
    drop_callbacks(m, handle);
    lua_pushboolean(L, 1);
    return 1;
}

void khdays_api_screens_open(KhdaysMod *m)
{
    static const luaL_Reg screen[] = {
        {"list", list}, {"page", page}, {"result", result}, {"close", close_screen}, {NULL, NULL}};
    static const luaL_Reg title[] = {{"entry", entry}, {NULL, NULL}};
    m->v2->owner = ++next_owner;
    m->v2->loading = 1;
    luaL_newlib(m->L, screen);
    lua_setglobal(m->L, "screen");
    luaL_newlib(m->L, title);
    lua_setglobal(m->L, "title");
}

void khdays_api_screens_release(KhdaysMod *m)
{
    if (m->v2 == NULL) {
        return;
    }
    khdays_screens_forget_owner(m->v2->owner);
    while (m->v2->callback_count > 0) {
        drop_callbacks(m, m->v2->callbacks[0].handle);
    }
    free(m->v2->callbacks);
    m->v2->callbacks = NULL;
    m->v2->callback_room = 0;
}

static KhdaysMod *owner_mod(int owner)
{
    for (int i = 0; i < khdays_scripts_mod_count(); ++i) {
        KhdaysMod *m = khdays_scripts_mod(i);
        if (m->v2 != NULL && m->v2->owner == owner) {
            return m;
        }
    }
    return NULL;
}

void khdays_api_screens_frame(void)
{
    KhdaysScreenAnswer a;
    while (khdays_screens_take(&a)) {
        KhdaysMod *m = owner_mod(a.owner);
        int ref = LUA_NOREF, args = 0;
        if (m == NULL || m->state != KHDAYS_MOD_LOADED) {
            continue;
        }
        for (int i = 0; i < m->v2->callback_count; ++i) {
            const KhdaysModCallbacks *c = &m->v2->callbacks[i];
            if (c->handle == a.handle) {
                ref = a.kind == KHDAYS_ANSWER_CHOOSE   ? c->on_choose
                      : a.kind == KHDAYS_ANSWER_CANCEL ? c->on_cancel
                      : a.kind == KHDAYS_ANSWER_CLOSE  ? c->on_close
                                                       : c->on_select;
            }
        }
        if (a.kind != KHDAYS_ANSWER_ENTRY) {
            /* the screen is gone: its functions with it, once the one called is on the stack */
            if (ref != LUA_NOREF) {
                lua_rawgeti(m->L, LUA_REGISTRYINDEX, ref);
            }
            drop_callbacks(m, a.handle);
        } else if (ref != LUA_NOREF) {
            lua_rawgeti(m->L, LUA_REGISTRYINDEX, ref);
        }
        if (ref == LUA_NOREF) {
            continue;
        }
        if (a.kind == KHDAYS_ANSWER_CHOOSE) {
            lua_pushinteger(m->L, a.index + 1);
            args = 1;
        }
        khdays_script_events_hold();
        khdays_sandbox_call(m, args, 0);
        khdays_script_events_release();
    }
}
