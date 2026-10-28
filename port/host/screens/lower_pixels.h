/* The DS's pixels the lower screens are told by (lower_layout.h khdays_lower_sample: 0x00RRGGBB, row by
 * row), and the rows a screen is laid out by that lower_screens.c and lower_pages.c share. */
#ifndef KHDAYS_LOWER_PIXELS_H
#define KHDAYS_LOWER_PIXELS_H

#include "lower_layout.h"
#include "lower_screens.h"

#include <stdint.h>

#define W KHDAYS_LOWER_DS_W
#define H KHDAYS_LOWER_DS_H

static inline uint32_t at(const uint32_t *ds, int x, int y)
{
    return ds[y * W + x];
}
static inline int red(uint32_t c)
{
    return (int)(c >> 16 & 255u);
}
static inline int green(uint32_t c)
{
    return (int)(c >> 8 & 255u);
}
static inline int blue(uint32_t c)
{
    return (int)(c & 255u);
}
static inline int sum(uint32_t c)
{
    return red(c) + green(c) + blue(c);
}
static inline int grey(uint32_t c)
{
    return red(c) == green(c) && green(c) == blue(c);
}
static inline int uniform_row(const uint32_t *ds, int y)
{
    for (int x = 1; x < W; ++x) {
        if (at(ds, x, y) != at(ds, 0, y)) {
            return 0;
        }
    }
    return 1;
}
/* Row y one colour from x0 to x1 (not including x1). */
static inline int uniform_span(const uint32_t *ds, int y, int x0, int x1)
{
    for (int x = x0 + 1; x < x1; ++x) {
        if (at(ds, x, y) != at(ds, x0, y)) {
            return 0;
        }
    }
    return 1;
}

/* Rows for a picture of `rows` rows: the DS's, each of inserts[i][0] repeated, the extra rows shared by
 * inserts[i][1]'s weight. */
void khdays_lower_spread(int16_t *out, int rows, const int (*inserts)[2], int count);

/* A place's map by its rows: in each column band (split at place->split, place->split2) the source row of
 * each row of the taller picture, place->rows. */
int khdays_lower_map_rows(const KhdaysLowerPlace *p, float ox, float oy, float *sx, float *sy);

/* The row from y0 to y1 whose columns x0 to x1 the most rows share: a stretch of a frame or a scroll
 * track plain, not where the pointing hand, the chosen entry or the track's thumb is. */
int khdays_lower_plain_row(const uint32_t *ds, int y0, int y1, int x0, int x1);

/* The lists drawn a page at a time and a picture of one colour (lower_pages.c): KHDAYS_LOWER_* or NONE. */
int khdays_lower_pages_tell(const uint32_t *ds);

/* Fills `place` for one of those screens on a picture `rows` DS rows high (lower_screens.h). */
int khdays_lower_pages_place(int screen, const uint32_t *ds, int rows, KhdaysLowerPlace *place, int *key);

#endif
