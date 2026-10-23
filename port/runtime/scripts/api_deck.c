/* The deck a mod reads and changes (api_v2.h; deck_link.h is the game's side). A change is made on a
 * copy, checked whole by the camp's own rules, then written; refused, nothing changes. The rules: a
 * panel takes its own cell or its shape's cells, on open cells and over no other; a link panel sits on
 * a multi-cell panel's cell; one gear, one ring or charm, one multi-cell ability of a kind
 * (docs/RUNTIME_MAP.md, "The deck"). */
#include "api_v2.h"

#include "api.h"
#include "deck_link.h"
#include "game_link.h"

#include <lauxlib.h>
#include <lua.h>

#include <string.h>

#define COPIES_MAX 99 /* Ov008_AddItemCount's cap */
#define CATEGORY_ABILITY 5
#define CATEGORY_ACCESSORY 6
#define GEAR_FIRST 0xbf
#define GEAR_LAST 0xf0

/* The deck as a change sees it: its cells and the copies of each item owned. */
typedef struct Deck {
    unsigned short cells[KHDAYS_LINK_DECK_CELLS];
    unsigned char owned[KHDAYS_LINK_ITEMS];
} Deck;

static int refuse(lua_State *L, const char *why)
{
    lua_pushnil(L);
    lua_pushstring(L, why);
    return 2;
}

static int read_deck(Deck *d)
{
    return khdays_link_deck(d->cells) && khdays_link_items(d->owned);
}

static int open_cells(const Deck *d)
{
    const int open = d->owned[KHDAYS_LINK_DECK_OPENER] + KHDAYS_LINK_DECK_OPEN;
    return open < KHDAYS_LINK_DECK_CELLS ? open : KHDAYS_LINK_DECK_CELLS;
}

static int is_link(const KhdaysLinkPanel *p)
{
    return p->shape < 0 && (p->category == 4 || p->category == 5);
}

static int is_gear(int id)
{
    return id >= GEAR_FIRST && id <= GEAR_LAST;
}

/* Whether the deck holds, but at cell `but`, a panel that panel `p` (item `id`) may not join: another
 * gear, or one of its category and kind where the deck holds one of a kind. */
static int clashes(const Deck *d, int id, const KhdaysLinkPanel *p, int but)
{
    const int one_of_a_kind = p->category == CATEGORY_ACCESSORY || (p->category == CATEGORY_ABILITY && p->shape >= 0);
    for (int c = 0; c < KHDAYS_LINK_DECK_CELLS && (is_gear(id) || one_of_a_kind); ++c) {
        KhdaysLinkPanel q;
        if (c == but || d->cells[c] == 0 || !khdays_link_panel(d->cells[c], &q)) {
            continue;
        }
        if ((is_gear(id) && is_gear(d->cells[c])) ||
            (one_of_a_kind && q.category == p->category && q.kind == p->kind && (q.shape >= 0) == (p->shape >= 0))) {
            return 1;
        }
    }
    return 0;
}

/* Why the deck cannot be held as it is (NULL when it can). `under` gets, for each cell, the cell of the
 * multi-cell panel covering it (-1 none). */
static const char *check(const Deck *d, short *under)
{
    unsigned char set[KHDAYS_LINK_ITEMS] = {0};
    const int open = open_cells(d);
    for (int c = 0; c < KHDAYS_LINK_DECK_CELLS; ++c) {
        under[c] = -1;
    }
    for (int c = 0; c < KHDAYS_LINK_DECK_CELLS; ++c) {
        KhdaysLinkPanel p;
        const int id = d->cells[c];
        if (id == 0) {
            continue;
        }
        if (id >= KHDAYS_LINK_ITEMS || !khdays_link_panel(id, &p)) {
            return "unknown panel";
        }
        if (++set[id] > d->owned[id] || c >= open || (p.shape >= 0 && !khdays_link_cover(&p, c, open, under))) {
            return "does not fit";
        }
    }
    for (int c = 0; c < KHDAYS_LINK_DECK_CELLS; ++c) {
        KhdaysLinkPanel p;
        if (d->cells[c] == 0 || !khdays_link_panel(d->cells[c], &p)) {
            continue;
        }
        if (clashes(d, d->cells[c], &p, c)) {
            return "does not fit";
        }
        if (p.shape < 0 && (is_link(&p) ? under[c] < 0 : p.category == CATEGORY_ACCESSORY && under[c] >= 0)) {
            return "does not fit";
        }
    }
    return NULL;
}

/* Whether panel `p` (item `id`) may be set at the empty cell `at` of the deck as `check` laid it out
 * (`under`), as the camp lets the player: a multi-cell panel over empty cells only, a link panel on a
 * multi-cell panel of its category or kind, any other on a cell of its own or such a panel's. */
static int may_set(const Deck *d, int id, const KhdaysLinkPanel *p, int at, const short *under)
{
    const int open = open_cells(d);
    if (d->cells[at] != 0 || at >= open || clashes(d, id, p, -1)) {
        return 0;
    }
    if (p->shape >= 0) {
        short trial[KHDAYS_LINK_DECK_CELLS];
        memcpy(trial, under, sizeof(trial));
        if (!khdays_link_cover(p, at, open, trial)) {
            return 0;
        }
        for (int c = 0; c < KHDAYS_LINK_DECK_CELLS; ++c) {
            if (trial[c] == at && d->cells[c] != 0) {
                return 0;
            }
        }
        return 1;
    }
    if (under[at] >= 0) {
        KhdaysLinkPanel host;
        return p->category != CATEGORY_ACCESSORY && khdays_link_panel(d->cells[under[at]], &host) &&
               (host.category == p->category || host.kind == p->kind);
    }
    return !is_link(p);
}

/* The deck changed to `d` when it can be held, else why not. */
static int write(lua_State *L, const Deck *d)
{
    short under[KHDAYS_LINK_DECK_CELLS];
    Deck now;
    const char *why = check(d, under);
    if (why != NULL) {
        return refuse(L, why);
    }
    if (!read_deck(&now)) {
        return refuse(L, "no game");
    }
    for (int id = 0; id < KHDAYS_LINK_ITEMS; ++id) {
        if (d->owned[id] != now.owned[id]) {
            khdays_link_set_item(id, d->owned[id]);
        }
    }
    khdays_link_set_deck(d->cells);
    lua_pushboolean(L, 1);
    return 1;
}

/* For a change: this mod may change the game now and the deck is read, else the refusal is pushed. */
static int may_change(lua_State *L, Deck *d)
{
    const char *busy;
    if (!khdays_api_may_change(L, 1)) {
        return 0;
    }
    busy = khdays_link_deck_busy();
    if (busy != NULL || !read_deck(d)) {
        refuse(L, busy != NULL ? busy : "no game");
        return 0;
    }
    return 1;
}

static int panel_arg(lua_State *L, int arg, KhdaysLinkPanel *p)
{
    const lua_Integer id = luaL_checkinteger(L, arg);
    return id > 0 && id < KHDAYS_LINK_ITEMS && khdays_link_panel((int)id, p) ? (int)id : 0;
}

static void set_int(lua_State *L, const char *name, lua_Integer v)
{
    lua_pushinteger(L, v);
    lua_setfield(L, -2, name);
}

/* deck.read(): { open = cells open, panels = { {panel, page, row, col}, ... } in the deck's order,
 * spare = { [panel] = copies owned and not set } }. */
static int read(lua_State *L)
{
    unsigned char set[KHDAYS_LINK_ITEMS] = {0};
    Deck d;
    int n = 0;
    if (!read_deck(&d)) {
        return refuse(L, "no game");
    }
    lua_createtable(L, 0, 3);
    set_int(L, "open", open_cells(&d));
    lua_newtable(L);
    for (int c = 0; c < KHDAYS_LINK_DECK_CELLS; ++c) {
        if (d.cells[c] == 0) {
            continue;
        }
        if (d.cells[c] < KHDAYS_LINK_ITEMS) {
            ++set[d.cells[c]];
        }
        lua_createtable(L, 0, 4);
        set_int(L, "panel", d.cells[c]);
        set_int(L, "page", c / KHDAYS_LINK_DECK_PAGE + 1);
        set_int(L, "row", c % KHDAYS_LINK_DECK_PAGE / KHDAYS_LINK_DECK_COLS + 1);
        set_int(L, "col", c % KHDAYS_LINK_DECK_COLS + 1);
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "panels");
    lua_newtable(L);
    for (int id = 1; id < KHDAYS_LINK_ITEMS; ++id) {
        KhdaysLinkPanel p;
        if (d.owned[id] > set[id] && khdays_link_panel(id, &p)) {
            lua_pushinteger(L, d.owned[id] - set[id]);
            lua_rawseti(L, -2, id);
        }
    }
    lua_setfield(L, -2, "spare");
    return 1;
}

/* deck.info(panel): { category, kind, link (set on a multi-cell panel's cell), cells = { {row, col},
 * ... } the cells it takes from its own }. */
static int info(lua_State *L)
{
    KhdaysLinkPanel p;
    int n = 0;
    if (!panel_arg(L, 1, &p)) {
        return refuse(L, "unknown panel");
    }
    lua_createtable(L, 0, 4);
    set_int(L, "category", p.category);
    set_int(L, "kind", p.kind);
    lua_pushboolean(L, is_link(&p));
    lua_setfield(L, -2, "link");
    lua_newtable(L);
    for (int bit = 0; bit < 64; ++bit) {
        if (p.shape < 0 ? bit == 0 : (p.cells >> bit & 1u) != 0) {
            lua_createtable(L, 0, 2);
            set_int(L, "row", p.shape < 0 ? 0 : bit / 8 - p.row);
            set_int(L, "col", p.shape < 0 ? 0 : bit % 8 - p.col);
            lua_rawseti(L, -2, ++n);
        }
    }
    lua_setfield(L, -2, "cells");
    return 1;
}

/* The cell of `at` ({page, row, col}, from 1), -1 when it is no cell of the deck. */
static int cell_of(lua_State *L, int at)
{
    lua_Integer v[3];
    static const char *const names[3] = {"page", "row", "col"};
    static const lua_Integer most[3] = {KHDAYS_LINK_DECK_CELLS / KHDAYS_LINK_DECK_PAGE, KHDAYS_LINK_DECK_ROWS,
                                        KHDAYS_LINK_DECK_COLS};
    for (int i = 0; i < 3; ++i) {
        int ok;
        lua_getfield(L, at, names[i]);
        v[i] = lua_tointegerx(L, -1, &ok);
        lua_pop(L, 1);
        if (!ok || v[i] < 1 || v[i] > most[i]) {
            return -1;
        }
    }
    return (int)((v[0] - 1) * KHDAYS_LINK_DECK_PAGE + (v[1] - 1) * KHDAYS_LINK_DECK_COLS + v[2] - 1);
}

/* deck.give(panel[, at]): a copy more, as the game's rewards give one (spare, for the camp's Panels);
 * set at `at` ({page, row, col}) or, with `true`, at the first cell it fits. */
static int give(lua_State *L)
{
    KhdaysLinkPanel p;
    const int id = panel_arg(L, 1, &p);
    Deck d;
    if (!lua_isnoneornil(L, 2) && !lua_istable(L, 2) && !lua_isboolean(L, 2)) {
        return luaL_argerror(L, 2, "a cell {page, row, col} or true");
    }
    if (id == 0) {
        return refuse(L, "unknown panel");
    }
    if (!may_change(L, &d)) {
        return 2;
    }
    if (d.owned[id] >= COPIES_MAX) {
        return refuse(L, "no room");
    }
    ++d.owned[id];
    if (lua_istable(L, 2) || lua_toboolean(L, 2)) {
        short under[KHDAYS_LINK_DECK_CELLS];
        const int wanted = lua_istable(L, 2) ? cell_of(L, 2) : -1;
        int at = -1;
        if (lua_istable(L, 2) && wanted < 0) {
            return luaL_argerror(L, 2, "a cell {page, row, col} of the deck");
        }
        if (check(&d, under) != NULL) {
            return refuse(L, "does not fit"); /* the deck as it is already */
        }
        for (int c = wanted >= 0 ? wanted : 0; at < 0 && c < KHDAYS_LINK_DECK_CELLS; ++c) {
            if (may_set(&d, id, &p, c, under)) {
                at = c;
            }
            if (wanted >= 0) {
                break;
            }
        }
        if (at < 0) {
            return refuse(L, "no room");
        }
        d.cells[at] = (unsigned short)id;
    }
    return write(L, &d);
}

/* deck.take(panel[, n]): n copies (1) fewer, the spare ones first, then the deck's from its last cell
 * (as Ov004_RemoveLoadoutEntries takes them). */
static int take(lua_State *L)
{
    KhdaysLinkPanel p;
    const int id = panel_arg(L, 1, &p);
    const lua_Integer n = luaL_optinteger(L, 2, 1);
    int set = 0, from_deck;
    Deck d;
    if (n < 1) {
        return luaL_argerror(L, 2, "a count of 1 or more");
    }
    if (id == 0) {
        return refuse(L, "unknown panel");
    }
    if (!may_change(L, &d)) {
        return 2;
    }
    if (n > d.owned[id]) {
        return refuse(L, "not there");
    }
    for (int c = 0; c < KHDAYS_LINK_DECK_CELLS; ++c) {
        set += d.cells[c] == id;
    }
    from_deck = (int)n - (d.owned[id] - set);
    for (int c = KHDAYS_LINK_DECK_CELLS - 1; c >= 0 && from_deck > 0; --c) {
        if (d.cells[c] == id) {
            d.cells[c] = 0;
            --from_deck;
        }
    }
    d.owned[id] = (unsigned char)(d.owned[id] - n);
    return write(L, &d);
}

/* deck.save(): { grid = { the deck's cells' panels, 0 empty }, owned = { [panel] = copies } }. */
static int save(lua_State *L)
{
    Deck d;
    if (!read_deck(&d)) {
        return refuse(L, "no game");
    }
    lua_createtable(L, 0, 2);
    lua_createtable(L, KHDAYS_LINK_DECK_CELLS, 0);
    for (int c = 0; c < KHDAYS_LINK_DECK_CELLS; ++c) {
        lua_pushinteger(L, d.cells[c]);
        lua_rawseti(L, -2, c + 1);
    }
    lua_setfield(L, -2, "grid");
    lua_newtable(L);
    for (int id = 1; id < KHDAYS_LINK_ITEMS; ++id) {
        KhdaysLinkPanel p;
        if (d.owned[id] > 0 && khdays_link_panel(id, &p)) {
            lua_pushinteger(L, d.owned[id]);
            lua_rawseti(L, -2, id);
        }
    }
    lua_setfield(L, -2, "owned");
    return 1;
}

/* deck.restore(saved): the deck and the panels' copies as `saved` has them (deck.save's form); refused
 * whole when it cannot be held. */
static int restore(lua_State *L)
{
    Deck d;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);
    if (lua_getfield(L, 1, "grid") != LUA_TTABLE) {
        return luaL_argerror(L, 1, "a saved deck (deck.save)");
    }
    if (lua_getfield(L, 1, "owned") != LUA_TTABLE) {
        return luaL_argerror(L, 1, "a saved deck (deck.save)");
    }
    if (!may_change(L, &d)) {
        return 2;
    }
    for (int c = 0; c < KHDAYS_LINK_DECK_CELLS; ++c) {
        int ok;
        lua_Integer v;
        lua_rawgeti(L, 2, c + 1);
        v = lua_tointegerx(L, -1, &ok);
        lua_pop(L, 1);
        if (!ok || v < 0 || v >= KHDAYS_LINK_ITEMS) {
            return refuse(L, "unknown panel");
        }
        d.cells[c] = (unsigned short)v;
    }
    for (int id = 1; id < KHDAYS_LINK_ITEMS; ++id) {
        KhdaysLinkPanel p;
        int ok = 1;
        lua_Integer v = 0;
        if (!khdays_link_panel(id, &p)) {
            continue;
        }
        if (lua_rawgeti(L, 3, id) != LUA_TNIL) {
            v = lua_tointegerx(L, -1, &ok);
        }
        if (!ok || v < 0 || v > COPIES_MAX) {
            return refuse(L, "does not fit");
        }
        lua_pop(L, 1);
        d.owned[id] = (unsigned char)v;
    }
    return write(L, &d);
}

void khdays_api_deck_open(KhdaysMod *m)
{
    static const luaL_Reg deck[] = {{"read", read}, {"info", info},       {"give", give}, {"take", take},
                                    {"save", save}, {"restore", restore}, {NULL, NULL}};
    luaL_newlib(m->L, deck);
    lua_setglobal(m->L, "deck");
}
