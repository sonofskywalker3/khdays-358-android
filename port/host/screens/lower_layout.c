/* lower_layout.h: the map from each display pixel to the picture's (lower_screens.c says where, screen by
 * screen, in DS dots), made once for a screen and its measurements, then a copy per frame. */
#include "lower_layout.h"
#include "lower_screens.h"

#include <stdlib.h>
#include <string.h>

#define COL_BITS 12 /* a map entry: the picture's row above these bits, its column in them */
#define COL_MASK ((1 << COL_BITS) - 1)
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
    const int rows = KHDAYS_LOWER_DS_H;
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

int khdays_lower_layout(KhdaysLowerLayout *layout, const KhdaysLowerPicture *picture, const uint32_t *ds, int ds_rows,
                        int screen, int width, int height)
{
    KhdaysLowerPlace place;
    const float scale = (float)width / KHDAYS_LOWER_DS_W; /* display pixels a DS dot */
    const float src_scale = (float)picture->width / KHDAYS_LOWER_DS_W;
    const int over_rows = picture->overscan != NULL ? picture->overscan_rows : 0;
    int key;
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
            screen = KHDAYS_LOWER_NONE;
            key = 0;
        }
    }
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
                sx = ox;
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
        }
    }
    return 1;
}

void khdays_lower_draw(const KhdaysLowerLayout *layout, const KhdaysLowerPicture *picture, uint8_t *out, int pitch)
{
    for (int y = 0; y < layout->height; ++y) {
        const int32_t *map = layout->map + (size_t)y * (size_t)layout->width;
        uint32_t *dst = (uint32_t *)(out + (size_t)y * (size_t)pitch);
        for (int x = 0; x < layout->width; ++x) {
            const int32_t v = map[x];
            if (v < 0) {
                dst[x] = OPAQUE_BLACK;
            } else {
                dst[x] = ((const uint32_t *)row_of(picture, v >> COL_BITS))[v & COL_MASK];
            }
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
    if (v < 0 || (v >> COL_BITS) >= layout->src_h) {
        return 0;
    }
    *ds_x = (v & COL_MASK) * KHDAYS_LOWER_DS_W / layout->src_w;
    *ds_y = (v >> COL_BITS) * KHDAYS_LOWER_DS_H / layout->src_h;
    return 1;
}

void khdays_lower_free(KhdaysLowerLayout *layout)
{
    free(layout->map);
    memset(layout, 0, sizeof(*layout));
}
