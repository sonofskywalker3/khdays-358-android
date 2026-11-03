/* The screen that moves the HUD's groups, with the mouse or a finger, and on a touch screen the on-screen controls
 * with them, over the picture the game stopped at. It knows the layouts (ui/hud_layout.h, host/touch/) and nothing of
 * how pictures are made. */
#ifndef KHDAYS_LAYOUT_EDITOR_SCREEN_H
#define KHDAYS_LAYOUT_EDITOR_SCREEN_H

#include <SDL3/SDL.h>

#include "../app/profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opens it on the player's layout. */
void khdays_layout_editor_open(SDL_Renderer *renderer);

int khdays_layout_editor_is_open(void);

/* One SDL event while it is open. Returns 1 when it has closed; *changed is set when the profile was changed by that
 * (the caller saves it). Sets *moved when the picture is to be made again for a group's new place. */
int khdays_layout_editor_event(const SDL_Event *event, SDL_Renderer *renderer, int *changed, int *moved);

/* Over the frame. */
void khdays_layout_editor_draw(SDL_Renderer *renderer);

#ifdef __cplusplus
}
#endif

#endif
