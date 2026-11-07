/* lower_held.h: a panel held by a finger on the Panels page, drawn whole on the taller display.
 *
 * The page keeps its layout while a finger is down (lower_layout.h khdays_lower_hold): the board's grid
 * centred, its extra rows repeating one plain row of its frame above the grid and one below. The game
 * draws the panel being dragged under the finger and its label (N/A) right above it; where they cross
 * one of those rows the extra rows came between them, the label left above a gap. Here the part of the
 * held panel and label past such a row is drawn again against the rest, as the game drew them, and the
 * frame is drawn where it was: from the frame as it was the last time no finger was down. Nothing else
 * on the page moves. */
#ifndef KHDAYS_LOWER_HELD_H
#define KHDAYS_LOWER_HELD_H

#include "lower_layout.h"

#include <stdint.h>

#define KHDAYS_LOWER_SEAMS 2

/* A repeated row a held panel can cross, in DS dots: the rows of the band [band0, band1) are drawn
 * against row `anchor` (just past the band, on the grid's side) as the DS has them; the board's columns
 * up to x1; only with the finger at row finger_y0 or below (above it: the board's tabs). */
typedef struct KhdaysLowerSeam {
    int anchor, band0, band1, x1, finger_y0;
} KhdaysLowerSeam;

/* The seams of a screen (lower_pages.c), at most KHDAYS_LOWER_SEAMS: how many (0 for a screen with none). */
int khdays_lower_seams(int screen, KhdaysLowerSeam *seams);

/* The seams' bands as they are with no finger down, for the next touch. */
typedef struct KhdaysLowerHeld {
    uint8_t *clean[KHDAYS_LOWER_SEAMS];
    int size[KHDAYS_LOWER_SEAMS]; /* bytes held */
    int src_w, src_h, screen, valid;
} KhdaysLowerHeld;

/* With no finger down, after the page is drawn: its seams' bands kept (forgotten on a screen with none). */
void khdays_lower_held_note(KhdaysLowerHeld *held, const KhdaysLowerLayout *layout, const KhdaysLowerPicture *picture);

/* With a finger down at DS dot (ds_x, ds_y) and the layout held, after khdays_lower_draw: the panel held
 * drawn whole across a seam it crosses, the frame drawn there as kept. Draws nothing when nothing is kept
 * for this picture, or when a band changed away from the finger too (the page dimmed, a tab changed). */
void khdays_lower_held_draw(const KhdaysLowerHeld *held, const KhdaysLowerLayout *layout,
                            const KhdaysLowerPicture *picture, int ds_x, int ds_y, uint8_t *out, int pitch);

void khdays_lower_held_forget(KhdaysLowerHeld *held);
void khdays_lower_held_free(KhdaysLowerHeld *held);

#endif
