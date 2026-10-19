/* MODO CINE's list on the screen (cine_screen.c): what runtime/menus/cinelist.h says the game's list
 * is, drawn over the whole window as the theater of KINGDOM HEARTS HD 1.5+2.5 ReMIX draws its
 * chapters, in a mod's pieces (layouts/cine/<lang>/cine.txt, tools/hd/theater_cine.py). With the
 * theater's chapters there and their HD videos (Video `hd`), the list is the port's: its chapters,
 * its cursor, A playing a chapter's videos in a row; else the game's scenes, words and cursor. The
 * window's thread only. */
#ifndef KHDAYS_CINE_SCREEN_H
#define KHDAYS_CINE_SCREEN_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the list over whatever was rendered when it shows; nothing otherwise. Returns whether it
 * drew. */
int khdays_cine_screen_draw(SDL_Renderer *renderer);

/* The player's DS buttons, once a turn of the window's loop: the port's list of chapters moves
 * (UP and DOWN a row, LEFT and RIGHT a page) and A plays the chapter selected. */
void khdays_cine_screen_buttons(uint16_t buttons);

/* The renderer is about to go: its textures are freed (made again on the next draw). */
void khdays_cine_screen_renderer_gone(void);

#ifdef __cplusplus
}
#endif

#endif
