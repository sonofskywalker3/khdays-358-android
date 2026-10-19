/* The tables a mod's scripts see: mod (api_mod.c), game, player, mission and scene (api_state.c). */
#ifndef KHDAYS_SCRIPTS_API_H
#define KHDAYS_SCRIPTS_API_H

#include "mod.h"

void khdays_api_mod_open(KhdaysMod *mod);
void khdays_api_state_open(KhdaysMod *mod);

/* mod.option (api_menus.c): the options' table, freed with the mod's state; a change the player made
 * (an option's value, a button pressed) kept and handed to the option's code. */
void khdays_api_menus_open(KhdaysMod *mod);

/* The game's actions (api_actions.c): items given and taken, a sound, the field's notice. */
void khdays_api_actions_open(KhdaysMod *mod);
void khdays_api_menus_free(KhdaysMod *mod);
void khdays_api_menus_use(KhdaysMod *mod, const char *key, double value);

/* For a name that changes the game: 1 when this mod may now, else pushes nil and the reason and
 * returns 0 (a visual mod, a guest in a multiplayer session, no game yet). */
int khdays_api_may_change(struct lua_State *L, int needs_state);

#endif
