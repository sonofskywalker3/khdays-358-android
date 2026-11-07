/* lower_layout.h: the map from each display pixel to the picture's (lower_screens.c says where, screen by
 * screen, in DS dots), made once for a screen and its measurements, then a copy per frame. */
#include "lower_layout.h"
#include "lower_map.h"
#include "lower_screens.h"

#include <stdlib.h>
#include <string.h>

#define OPAQUE_BLACK 0xff000000u

static const uint8_t *row_of(const KhdaysLowerPicture *p, int row)
{
    if (row < p->height) {
        return p->rgba + (size_t)row * (size_t)p->pitch;
    }
    return p->overscan + (size_t)(row - p->height) * (size_t)p->overscan_pitch;
}

void khdays_lower_sample(const KhdaysLowerPicture *picture, uint32_t *ds, int *ds_rows)
{
    int rows = KHDAYS_LOWER_DS_H;
    if (picture->overscan != NULL) {
        const int over = picture->overscan_rows * KHDAYS_LOWER_DS_H / picture->height;
        rows += over < KHDAYS_LOWER_OVERSCAN ? over : KHDAYS_LOWER_OVERSCAN;
    }
    for (int y = 0; y < rows; ++y) {
        const int py = (2 * y + 1) * picture->height / (2 * KHDAYS_LOWER_DS_H);
        const uint8_t *row = row_of(picture, py);
        for (int x = 0; x < KHDAYS_LOWER_DS_W; ++x) {
            const uint8_t *p = row + (size_t)((2 * x + 1) * picture->width / (2 * KHDAYS_LOWER_DS_W)) * 4u;
            ds[y * KHDAYS_LOWER_DS_W + x] = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        }
    }
    *ds_rows = rows;
}

int khdays_lower_screen(const uint32_t *ds, int ds_rows)
{
    return khdays_lower_screens_tell(ds, ds_rows);
}

#define FIELD_ROWS_HELD 30 /* frames */

/* This frame's copy table of a place (none for a screen that does not repeat rows so). */
static void take_copies(KhdaysLowerLayout *layout, const KhdaysLowerPlace *place, int valid)
{
    layout->copies = 0;
    if (!valid || place->map != khdays_lower_map_rows) {
        return;
    }
    layout->copy_split = place->split;
    layout->copy_split2 = place->split2;
    memcpy(layout->copy_slot, place->copy_slot, sizeof(layout->copy_slot));
    memcpy(layout->copy_from, place->copy_from, sizeof(layout->copy_from));
    for (int b = 0; b < 3 && !layout->copies; ++b) {
        for (int y = 0; y < KHDAYS_LOWER_COPY_H && !layout->copies; ++y) {
            layout->copies = layout->copy_slot[b][y] != 0;
        }
    }
}

/* The DS rows tall the display is laid out (its height in DS dots at the layout's scale). */
static float out_rows(int width, int height)
{
    const int wider = (long)width * KHDAYS_LOWER_DS_H > (long)height * KHDAYS_LOWER_DS_W;
    const float scale = wider ? (float)height / KHDAYS_LOWER_DS_H : (float)width / KHDAYS_LOWER_DS_W;
    return (float)height / scale;
}

void khdays_lower_copies_refresh(KhdaysLowerLayout *layout, const uint32_t *ds, int ds_rows)
{
    static KhdaysLowerPlace place; /* large: not on the stack */
    int key;
    take_copies(layout, &place,
                layout->screen != KHDAYS_LOWER_NONE &&
                    khdays_lower_screens_place(layout->screen, ds, ds_rows, out_rows(layout->width, layout->height),
                                               &place, &key));
}

int khdays_lower_layout(KhdaysLowerLayout *layout, const KhdaysLowerPicture *picture, const uint32_t *ds, int ds_rows,
                        int screen, int width, int height)
{
    static KhdaysLowerPlace place; /* large: not on the stack */
    /* display pixels a DS dot: the DS's width across, or on a display wider than 4:3 its height */
    const int wider = (long)width * KHDAYS_LOWER_DS_H > (long)height * KHDAYS_LOWER_DS_W;
    const float scale = wider ? (float)height / KHDAYS_LOWER_DS_H : (float)width / KHDAYS_LOWER_DS_W;
    const float src_scale = (float)picture->width / KHDAYS_LOWER_DS_W;
    const int over_rows = picture->overscan != NULL ? picture->overscan_rows : 0;
    int key;
    const int held_row0 = layout->want_row0, held_rows = layout->want_rows;
    const uint32_t held_off = layout->want_off;
    layout->want_rows = 0;
    if (!khdays_lower_screens_place(screen, ds, ds_rows, (float)height / scale, &place, &key)) {
        screen = KHDAYS_LOWER_NONE;
        key = 0;
    } else if (place.row_count > 0) {
        /* rows apart from the DS's: shown once the renderer gives them (the DS's shape until then) */
        layout->want_row0 = place.row0;
        layout->want_rows = place.row_count;
        layout->want_off = place.layers_off;
        if (picture->overscan == NULL || picture->overscan_row0 != place.row0 ||
            picture->overscan_rows < (int)((float)place.row_count * src_scale) - 1) {
            if (place.rows_optional) {
                place.row_count = 0;    /* the field's map box: its edge rows until its map comes */
                key ^= (int)(1u << 30); /* ... and made again when it does */
            } else {
                screen = KHDAYS_LOWER_NONE;
                key = 0;
            }
        }
        layout->want_held = place.rows_optional ? FIELD_ROWS_HELD : 0;
    }
    if (layout->want_rows == 0 && held_rows > 0 && layout->want_held > 0) {
        /* a frame the field is not told on (a notice over it, a fade's step): its rows stay asked for, so its next
         * frames have them rather than the box's edge rows while they come again */
        --layout->want_held;
        layout->want_row0 = held_row0;
        layout->want_rows = held_rows;
        layout->want_off = held_off;
    }
    take_copies(layout, &place, screen != KHDAYS_LOWER_NONE);
    if (layout->map != NULL && layout->screen == screen && layout->key == key && layout->width == width &&
        layout->height == height && layout->src_w == picture->width && layout->src_h == picture->height &&
        layout->rows_got == over_rows) {
        return 1;
    }
    layout->rows_got = over_rows;
    if (layout->map_size < width * height) {
        int32_t *grown = (int32_t *)realloc(layout->map, sizeof(int32_t) * (size_t)width * (size_t)height);
        if (grown == NULL) {
            return 0;
        }
        layout->map = grown;
        layout->map_size = width * height;
    }
    if (layout->map == NULL) {
        return 0; /* a display of no pixels: nothing to map */
    }
    layout->screen = screen;
    layout->key = key;
    layout->width = width;
    layout->height = height;
    layout->src_w = picture->width;
    layout->src_h = picture->height;
    for (int y = 0; y < height; ++y) {
        const float oy = ((float)y + 0.5f) / scale;
        int32_t *out = layout->map + (size_t)y * (size_t)width;
        for (int x = 0; x < width; ++x) {
            const float ox = ((float)x + 0.5f) / scale;
            float sx, sy;
            int px, py;
            if (screen == KHDAYS_LOWER_NONE) {
                /* the DS's shape, centred */
                sx = ox - ((float)width / scale - KHDAYS_LOWER_DS_W) / 2.0f;
                sy = oy - ((float)height / scale - KHDAYS_LOWER_DS_H) / 2.0f;
            } else if (!place.map(&place, ox, oy, &sx, &sy)) {
                out[x] = -1;
                continue;
            }
            px = (int)(sx * src_scale);
            if (sy >= KHDAYS_LOWER_ROWS) {
                /* the extra rows, under the picture */
                const int line = (int)((sy - KHDAYS_LOWER_ROWS - (float)picture->overscan_row0) * src_scale);
                py = line >= 0 && line < over_rows ? picture->height + line : -1;
            } else {
                py = (int)(sy * src_scale);
                py = py < picture->height ? py : -1;
            }
            if (sx < 0.0f || sy < 0.0f || px >= picture->width || py < 0) {
                out[x] = -1;
                continue;
            }
            out[x] = (int32_t)((uint32_t)py << COL_BITS | (uint32_t)px);
            if (screen != KHDAYS_LOWER_NONE && place.map == khdays_lower_map_rows && sy < KHDAYS_LOWER_ROWS &&
                khdays_lower_row_copy(&place, ox, oy)) {
                out[x] |= COPY_BIT;
            }
        }
    }
    return 1;
}

int khdays_lower_hold(const KhdaysLowerLayout *layout, const KhdaysLowerPicture *picture, int screen, int width,
                      int height, int touching)
{
    const int over_rows = picture->overscan != NULL ? picture->overscan_rows : 0;
    return touching && layout->map != NULL && layout->screen == KHDAYS_LOWER_PANELS &&
           (screen == KHDAYS_LOWER_PANELS || screen == KHDAYS_LOWER_NONE) && layout->width == width &&
           layout->height == height && layout->src_w == picture->width && layout->src_h == picture->height &&
           layout->rows_got == over_rows;
}

void khdays_lower_draw(const KhdaysLowerLayout *layout, const KhdaysLowerPicture *picture, uint8_t *out, int pitch)
{
    for (int y = 0; y < layout->height; ++y) {
        const int32_t *map = layout->map + (size_t)y * (size_t)layout->width;
        uint32_t *dst = (uint32_t *)(out + (size_t)y * (size_t)pitch);
        for (int x = 0; x < layout->width; ++x) {
            int32_t v = map[x];
            if (v < 0) {
                dst[x] = OPAQUE_BLACK;
                continue;
            }
            if (v & COPY_BIT) {
                v &= ~COPY_BIT;
                if (layout->copies) {
                    /* a copy whose column this frame takes from another like row (a mark on its own) */
                    /* the DS dot the picture's pixel lies in, by its middle (as a touch is) */
                    const int col = (2 * (v & COL_MASK) + 1) * KHDAYS_LOWER_DS_W / (2 * layout->src_w);
                    const int row = (2 * (v >> COL_BITS) + 1) * KHDAYS_LOWER_DS_H / (2 * layout->src_h);
                    const int band = col < layout->copy_split                                ? 0
                                     : layout->copy_split2 > 0 && col >= layout->copy_split2 ? 2
                                                                                             : 1;
                    const int slot = row < KHDAYS_LOWER_COPY_H ? layout->copy_slot[band][row] : 0;
                    const int from = slot > 0 ? layout->copy_from[band][slot - 1][col] : 0;
                    if (from > 0) {
                        v = (int32_t)((uint32_t)((2 * (from - 1) + 1) * layout->src_h / (2 * KHDAYS_LOWER_DS_H))
                                          << COL_BITS |
                                      (uint32_t)(v & COL_MASK));
                    }
                }
            }
            dst[x] = ((const uint32_t *)row_of(picture, v >> COL_BITS))[v & COL_MASK];
        }
    }
}

int khdays_lower_touch(const KhdaysLowerLayout *layout, int x, int y, int *ds_x, int *ds_y)
{
    int32_t v;
    if (layout->map == NULL || x < 0 || y < 0 || x >= layout->width || y >= layout->height) {
        return 0;
    }
    v = layout->map[(size_t)y * (size_t)layout->width + (size_t)x];
    v = v < 0 ? v : v & ~COPY_BIT; /* where the row is, not what it is drawn from */
    if (v < 0 || (layout->want_rows <= 0 && (v >> COL_BITS) >= layout->src_h)) {
        return 0;
    }
    if ((v >> COL_BITS) >= layout->src_h) {
        /* an extra row: past the DS's 192, as the game's list hook takes it (below the frame on the DS) */
        const int row = layout->want_row0 + ((v >> COL_BITS) - layout->src_h) * KHDAYS_LOWER_DS_H / layout->src_h;
        *ds_x = (2 * (v & COL_MASK) + 1) * KHDAYS_LOWER_DS_W / (2 * layout->src_w);
        *ds_y = row > KHDAYS_LOWER_DS_H ? row : KHDAYS_LOWER_DS_H;
        return 1;
    }
    /* the DS dot under the picture pixel's middle, as the picture is drawn */
    *ds_x = (2 * (v & COL_MASK) + 1) * KHDAYS_LOWER_DS_W / (2 * layout->src_w);
    *ds_y = (2 * (v >> COL_BITS) + 1) * KHDAYS_LOWER_DS_H / (2 * layout->src_h);
    if (layout->screen == KHDAYS_LOWER_MISSIONS) {
        /* the longer scroll track: its whole length for the game's, not the plain row its added rows show */
        khdays_lower_list_track_touch(*ds_x, (float)y / (float)layout->height, ds_y);
    }
    return 1;
}

void khdays_lower_standing_rows(int width, int height, int *row0, int *rows, uint32_t *layers_off)
{
    static const uint32_t blank[KHDAYS_LOWER_DS_W * (KHDAYS_LOWER_DS_H + KHDAYS_LOWER_OVERSCAN)] = {0};
    static KhdaysLowerPlace place; /* large: not on the stack (callers hold the lower screen's lock) */
    int key;
    *rows = 0;
    if (width > 0 && khdays_lower_screens_place(KHDAYS_LOWER_MISSIONS, blank, KHDAYS_LOWER_DS_H,
                                                (float)height * KHDAYS_LOWER_DS_W / (float)width, &place, &key)) {
        *row0 = place.row0;
        *rows = place.row_count;
        *layers_off = place.layers_off;
    }
}

void khdays_lower_free(KhdaysLowerLayout *layout)
{
    free(layout->map);
    memset(layout, 0, sizeof(*layout));
}
