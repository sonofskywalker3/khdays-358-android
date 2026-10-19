/* MODO CINE's list (cinelist.c), drawn as the HD collection's theater draws its chapters
 * (host/screens/cine_screen.c, a mod's layouts/cine/<lang>/cine.txt). The game's own list runs
 * underneath (its fades, its closing); with the theater's chapters and their HD videos the port's
 * list of them takes its place and its buttons, else the port shows the game's rows. */
#ifndef KHDAYS_CINELIST_H
#define KHDAYS_CINELIST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the game's rows (Ov000_FillListRows) */
#define KHDAYS_CINE_SCENES 28

typedef struct KhdaysCineView {
    int shown;
    int language;                        /* the game's (1-5), the one the player just chose while it applies */
    int first;                           /* the first row of the page shown (Ov000_DrawListScene's ten) */
    int cursor;                          /* the scene selected */
    int16_t master;                      /* the game's fade: its brightness, -16 black to 16 white */
    uint16_t movies[KHDAYS_CINE_SCENES]; /* each row's movie (its number: <n>.z in UI/thr/m.p2) */
} KhdaysCineView;

/* The game's thread: an object's update ran (diag.c, its native address). */
void khdays_cinelist_update_ran(uintptr_t update);
/* The game's thread, once a frame (input.c): what the list is, published. */
void khdays_cinelist_frame(void);
/* Any thread: what the list is, as last published. */
void khdays_cinelist_view(KhdaysCineView *out);

/* The window's thread: the port's own list of the theater's chapters shows over the game's
 * (host/screens/cine_screen.c), which then takes the buttons but B and START (the game's closing). */
void khdays_cinelist_take(int on);
/* The window's thread: a sound of the game's list's (Ov000's: 0 a move, 1 a choice) for the game's
 * thread to play. */
void khdays_cinelist_sound(int sound);
/* The game's thread, once a frame (input.c): the buttons the game's list gets. */
uint16_t khdays_cinelist_buttons(uint16_t buttons);
/* Any thread: the buttons the game's list would have got last frame (the player's and a script's),
 * for the port's list. */
uint16_t khdays_cinelist_seen(void);

#ifdef __cplusplus
}
#endif

#endif
