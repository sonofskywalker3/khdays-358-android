/* A mod's screens and the title's mods' prompt on the window (mod_screen.c): what
 * runtime/scripts/screens.h says shows. The window's thread only. */
#ifndef KHDAYS_MOD_SCREEN_H
#define KHDAYS_MOD_SCREEN_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the screen (or the title's prompt) over whatever was rendered; returns whether it drew. */
int khdays_mod_screen_draw(SDL_Renderer *renderer);

/* The renderer is about to go: its textures are freed (made again on the next draw). */
void khdays_mod_screen_renderer_gone(void);

#ifdef __cplusplus
}
#endif

#endif
