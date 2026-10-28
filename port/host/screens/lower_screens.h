/* The screens lower_layout.h lays out: how each is told from its pixels, and where each display point
 * (in the taller picture's DS dots) takes the DS screen's (lower_screens.c, the designs in
 * docs/LOWER_DISPLAY.md). */
#ifndef KHDAYS_LOWER_SCREENS_H
#define KHDAYS_LOWER_SCREENS_H

#include <stdint.h>

#define KHDAYS_LOWER_ROWS_MAX 320 /* the taller picture's rows, in DS dots, at most */

typedef struct KhdaysLowerPlace KhdaysLowerPlace;
struct KhdaysLowerPlace {
    /* Where (ox, oy) of the taller picture takes the DS screen's (sx, sy); rows from 192 on are those
     * the game drew past its screen. 0: black. */
    int (*map)(const KhdaysLowerPlace *place, float ox, float oy, float *sx, float *sy);
    float out_h; /* the taller picture's height, DS dots */
    float extra; /* out_h - 192 */
    int top, foot;
    float zoom, cx;
    int16_t rows[3][KHDAYS_LOWER_ROWS_MAX]; /* a row's source in each of up to three column bands, split at
                                               `split` and (when not 0) `split2` */
    int split, split2;
    /* extra rows it shows (a source row of KHDAYS_LOWER_ROWS + DS row): DS rows from row0, `rows` of them,
     * composed with layers_off's layers left out; row_count is how many (compose.h KD_LAYERS_OFF) */
    int row0, row_count;
    uint32_t layers_off;
};

/* KHDAYS_LOWER_* of the DS's pixels (lower_layout.h khdays_lower_sample). */
int khdays_lower_screens_tell(const uint32_t *ds, int ds_rows);

/* Fills `place` for `screen` on a picture out_h DS dots high, and `key` with what it measured; 0 when
 * the screen cannot be laid out (it then keeps the DS's shape). */
int khdays_lower_screens_place(int screen, const uint32_t *ds, int ds_rows, float out_h, KhdaysLowerPlace *place,
                               int *key);

#endif
