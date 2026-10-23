/* The interface's version 2, what its parts share (api_v2.h). */
#include "api_v2.h"

#include "api.h"
#include "game_link.h"

#include <lauxlib.h>
#include <lua.h>

#include <stdlib.h>

void khdays_api_v2_open(KhdaysMod *m)
{
    if (m->v2 == NULL) {
        m->v2 = calloc(1, sizeof(*m->v2));
    }
    khdays_api_enemies_open(m);
    khdays_api_run_open(m);
    khdays_api_screens_open(m);
    khdays_api_deck_open(m);
    khdays_api_time_open(m);
}

void khdays_api_v2_release(KhdaysMod *m)
{
    if (m->v2 == NULL) {
        return;
    }
    khdays_api_enemies_release(m);
    khdays_api_run_release(m);
    khdays_api_screens_release(m);
    khdays_api_time_release(m);
    free(m->v2);
    m->v2 = NULL;
}

int khdays_api_v2_may(lua_State *L, int where)
{
    static const char *const outside[] = {[KHDAYS_WHERE_FIELD] = "not in the field",
                                          [KHDAYS_WHERE_CAMP] = "not in a camp",
                                          [KHDAYS_WHERE_TITLE] = "not at the title"};
    if (!khdays_api_may_change(L, 1)) {
        return 0;
    }
    if (where != KHDAYS_WHERE_ELSEWHERE && khdays_link_where() != where) {
        lua_pushnil(L);
        lua_pushstring(L, outside[where]);
        return 0;
    }
    return 1;
}
