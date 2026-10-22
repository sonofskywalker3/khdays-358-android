/* The pause over an HD movie: the DS's scene pause (title, Continue, Skip) built from the game's own
 * pieces. The game has none over its movies; the port shows this one over the HD video instead
 * (runtime/movies/movie_hd.h). */
#ifndef KHDAYS_HOST_MOVIE_PAUSE_H
#define KHDAYS_HOST_MOVIE_PAUSE_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The window's thread: the panel as a 256 x 192 texture (clear around it) in the game's `language`
 * (1 English to 5 Spanish), entry `selected` lit (0 Continue, 1 Skip) in blink look `lit` (1 or 2);
 * NULL when the game's pieces cannot be read (logged once). */
SDL_Texture *khdays_movie_pause_panel(SDL_Renderer *renderer, int language, int selected, int lit);

/* The renderer is about to go (or to lose its textures): the panel's textures freed. */
void khdays_movie_pause_renderer_gone(void);

#ifdef __cplusplus
}
#endif

#endif
