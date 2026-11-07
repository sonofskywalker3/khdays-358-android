/* lower_held.h: a panel held on the Panels page drawn whole across the board's repeated rows. */
#include "lower_held.h"
#include "lower_map.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HELD_REACH 40 /* DS dots either side of the finger the held panel and its label can reach */

/* A seam in the picture's rows and columns. */
typedef struct Band {
    int anchor, row0, row1, x1;
} Band;

static Band band_of(const KhdaysLowerSeam *s, int src_w, int src_h)
{
    const Band b = {s->anchor * src_h / KHDAYS_LOWER_DS_H, s->band0 * src_h / KHDAYS_LOWER_DS_H,
                    s->band1 * src_h / KHDAYS_LOWER_DS_H, s->x1 * src_w / KHDAYS_LOWER_DS_W};
    return b;
}

void khdays_lower_held_forget(KhdaysLowerHeld *held)
{
    held->valid = 0;
}

void khdays_lower_held_free(KhdaysLowerHeld *held)
{
    for (int i = 0; i < KHDAYS_LOWER_SEAMS; ++i) {
        free(held->clean[i]);
    }
    memset(held, 0, sizeof(*held));
}

void khdays_lower_held_note(KhdaysLowerHeld *held, const KhdaysLowerLayout *layout, const KhdaysLowerPicture *picture)
{
    KhdaysLowerSeam seams[KHDAYS_LOWER_SEAMS];
    const int n = layout->map != NULL ? khdays_lower_seams(layout->screen, seams) : 0;
    held->valid = 0;
    for (int i = 0; i < n; ++i) {
        const Band b = band_of(&seams[i], picture->width, picture->height);
        const int size = (b.row1 - b.row0) * b.x1 * 4;
        if (size > held->size[i]) {
            uint8_t *grown = (uint8_t *)realloc(held->clean[i], (size_t)size);
            if (grown == NULL) {
                return;
            }
            held->clean[i] = grown;
            held->size[i] = size;
        }
        for (int r = b.row0; r < b.row1; ++r) {
            memcpy(held->clean[i] + (size_t)(r - b.row0) * (size_t)b.x1 * 4u,
                   picture->rgba + (size_t)r * (size_t)picture->pitch, (size_t)b.x1 * 4u);
        }
    }
    held->src_w = picture->width;
    held->src_h = picture->height;
    held->screen = layout->screen;
    held->valid = n > 0;
}

static uint32_t live_at(const KhdaysLowerPicture *p, int row, int col)
{
    return ((const uint32_t *)(p->rgba + (size_t)row * (size_t)p->pitch))[col];
}

static uint32_t clean_at(const uint8_t *clean, const Band *b, int row, int col)
{
    return ((const uint32_t *)clean)[(size_t)(row - b->row0) * (size_t)b->x1 + (size_t)col];
}

static int near_finger(int col, int src_w, int ds_x)
{
    return abs(col * KHDAYS_LOWER_DS_W / src_w - ds_x) <= HELD_REACH;
}

/* Whether the band differs from its kept frame only near the finger (the panel held and its label). */
static int only_near(const uint8_t *clean, const Band *b, const KhdaysLowerPicture *p, int ds_x)
{
    for (int r = b->row0; r < b->row1; ++r) {
        for (int c = 0; c < b->x1; ++c) {
            if (!near_finger(c, p->width, ds_x) && live_at(p, r, c) != clean_at(clean, b, r, c)) {
                return 0;
            }
        }
    }
    return 1;
}

/* The picture's row a display pixel shows (its column in *col), -1 for none of the DS's (black, or an
 * extra row past them). */
static int row_shown(const KhdaysLowerLayout *layout, int x, int y, int *col)
{
    int32_t v = layout->map[(size_t)y * (size_t)layout->width + (size_t)x];
    if (v < 0) {
        return -1;
    }
    v &= ~COPY_BIT;
    *col = v & COL_MASK;
    return (v >> COL_BITS) < layout->src_h ? v >> COL_BITS : -1;
}

/* One seam's band drawn: the frame as kept, and over it the live pixels that differ from it near the
 * finger, at the rows that go on from the anchor as on the DS. */
static void draw_band(const uint8_t *clean, const Band *b, const KhdaysLowerLayout *layout,
                      const KhdaysLowerPicture *picture, int ds_x, uint8_t *out, int pitch)
{
    /* the picture's rows a display row: the layout never stretches, so as many as its columns */
    const float per_row = (float)layout->src_w / (float)layout->width;
    int mid = -1, anchor_y = -1, col = 0;
    /* a display column through the board, and the display row the anchor row starts at in it */
    for (int x = 0; x < layout->width && mid < 0; ++x) {
        if (row_shown(layout, x, layout->height / 2, &col) >= 0 && col >= b->x1 / 2) {
            mid = x;
        }
    }
    for (int y = 0; y < layout->height && mid >= 0 && anchor_y < 0; ++y) {
        if (row_shown(layout, mid, y, &col) >= b->anchor) {
            anchor_y = y;
        }
    }
    if (anchor_y < 0) {
        return;
    }
    for (int y = 0; y < layout->height; ++y) {
        const int row = row_shown(layout, mid, y, &col);
        const int joined = b->anchor + (int)floorf((float)(y - anchor_y) * per_row);
        const int joins = joined >= b->row0 && joined < b->row1;
        uint32_t *dst = (uint32_t *)(out + (size_t)y * (size_t)pitch);
        if (row < b->row0 || row >= b->row1) {
            continue;
        }
        for (int x = 0; x < layout->width; ++x) {
            const int r = row_shown(layout, x, y, &col);
            uint32_t px;
            if (r < b->row0 || r >= b->row1 || col >= b->x1) {
                continue;
            }
            px = clean_at(clean, b, r, col);
            if (joins && near_finger(col, picture->width, ds_x) &&
                live_at(picture, joined, col) != clean_at(clean, b, joined, col)) {
                px = live_at(picture, joined, col); /* the panel held, or its label */
            }
            dst[x] = px;
        }
    }
}

void khdays_lower_held_draw(const KhdaysLowerHeld *held, const KhdaysLowerLayout *layout,
                            const KhdaysLowerPicture *picture, int ds_x, int ds_y, uint8_t *out, int pitch)
{
    KhdaysLowerSeam seams[KHDAYS_LOWER_SEAMS];
    int n;
    if (!held->valid || layout->map == NULL || held->screen != layout->screen || held->src_w != picture->width ||
        held->src_h != picture->height || layout->src_w != picture->width || layout->src_h != picture->height) {
        return;
    }
    n = khdays_lower_seams(layout->screen, seams);
    for (int i = 0; i < n; ++i) {
        const Band b = band_of(&seams[i], picture->width, picture->height);
        if (ds_y >= seams[i].finger_y0 && only_near(held->clean[i], &b, picture, ds_x)) {
            draw_band(held->clean[i], &b, layout, picture, ds_x, out, pitch);
        }
    }
}
