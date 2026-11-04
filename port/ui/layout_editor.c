/* layout_editor.h */
#include "layout_editor.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int requested;

void khdays_editor_request(void)
{
    requested = 1;
}

int khdays_editor_take_request(void)
{
    const int asked = requested;
    requested = 0;
    return asked;
}

int khdays_editor_item_at(const KhdaysLayoutEditor *e, float x, float y)
{
    int best = -1;
    float best_area = 0.0f;
    for (int i = 0; i < e->count; ++i) {
        const KhdaysHudBox *b = &e->item[i].box;
        if (x >= b->x0 && x < b->x1 && y >= b->y0 && y < b->y1) {
            const float area = (b->x1 - b->x0) * (b->y1 - b->y0);
            if (best < 0 || area < best_area) {
                best = i;
                best_area = area;
            }
        }
    }
    return best;
}

int khdays_editor_press(KhdaysLayoutEditor *e, float x, float y)
{
    e->held = khdays_editor_item_at(e, x, y);
    if (e->held >= 0) {
        e->grab_x = x - e->item[e->held].box.x0;
        e->grab_y = y - e->item[e->held].box.y0;
    }
    return e->held;
}

static float kept_inside(float start, float size, float low, float high)
{
    if (start + size > high) {
        start = high - size;
    }
    return start < low ? low : start;
}

/* An item's corner kept so that it and its room lie inside the area. */
static void item_inside(const KhdaysLayoutEditor *e, const KhdaysEditorItem *it, float w, float h, float *x0, float *y0)
{
    const KhdaysHudBox *r = &it->room;
    *x0 = kept_inside(*x0 - r->x0, w + r->x0 + r->x1, e->area.x0, e->area.x1) + r->x0;
    *y0 = kept_inside(*y0 - r->y0, h + r->y0 + r->y1, e->area.y0, e->area.y1) + r->y0;
}

int khdays_editor_drag(KhdaysLayoutEditor *e, float x, float y)
{
    KhdaysHudBox *b;
    float w, h, x0, y0;
    if (e->held < 0) {
        return 0;
    }
    b = &e->item[e->held].box;
    w = b->x1 - b->x0;
    h = b->y1 - b->y0;
    x0 = x - e->grab_x;
    y0 = y - e->grab_y;
    item_inside(e, &e->item[e->held], w, h, &x0, &y0);
    if (x0 == b->x0 && y0 == b->y0) {
        return 0;
    }
    b->x0 = x0;
    b->y0 = y0;
    b->x1 = x0 + w;
    b->y1 = y0 + h;
    return 1;
}

int khdays_editor_release(KhdaysLayoutEditor *e)
{
    KhdaysHudBox *b;
    float w, h, x0, y0;
    const int held = e->held;
    e->held = -1;
    if (held < 0 || !e->snap || e->grid <= 0.0f) {
        return 0;
    }
    b = &e->item[held].box;
    w = b->x1 - b->x0;
    h = b->y1 - b->y0;
    x0 = roundf(b->x0 / e->grid) * e->grid;
    y0 = roundf(b->y0 / e->grid) * e->grid;
    item_inside(e, &e->item[held], w, h, &x0, &y0);
    if (x0 == b->x0 && y0 == b->y0) {
        return 0;
    }
    b->x0 = x0;
    b->y0 = y0;
    b->x1 = x0 + w;
    b->y1 = y0 + h;
    return 1;
}

void khdays_editor_cutout(unsigned char *with, int with_pitch, const unsigned char *without, int without_pitch, int w, int h)
{
    enum { DIFFERS = 6 }; /* out of 255: what a picture's rounding does not reach */
    for (int y = 0; y < h; ++y) {
        unsigned char *a = with + (size_t)y * (size_t)with_pitch;
        const unsigned char *b = without + (size_t)y * (size_t)without_pitch;
        for (int x = 0; x < w; ++x, a += 4, b += 4) {
            const int differs = abs(a[0] - b[0]) > DIFFERS || abs(a[1] - b[1]) > DIFFERS || abs(a[2] - b[2]) > DIFFERS;
            a[3] = differs ? 255 : 0;
        }
    }
}

KhdaysHudPlace khdays_editor_place(const KhdaysHudPlace *now, const KhdaysHudBox *from, const KhdaysHudBox *to, int group,
                                   int columns)
{
    const float dx = to->x0 - from->x0, dy = to->y0 - from->y0;
    KhdaysHudPlace moved = *now;
    moved.set = 1;
    moved.x += moved.side_x == KHDAYS_HUD_AT_END ? -dx : dx;
    moved.y += moved.side_y == KHDAYS_HUD_AT_END ? -dy : dy;
    return khdays_hud_place_nearest(&moved, to, group, columns);
}

KhdaysHudPlace khdays_editor_control_place(const KhdaysHudBox *box, float w, float h, float unit)
{
    KhdaysHudPlace p;
    memset(&p, 0, sizeof(p));
    p.set = 1;
    p.side_x = (box->x0 + box->x1) / 2.0f >= w / 2.0f ? KHDAYS_HUD_AT_END : KHDAYS_HUD_AT_START;
    p.side_y = (box->y0 + box->y1) / 2.0f >= h / 2.0f ? KHDAYS_HUD_AT_END : KHDAYS_HUD_AT_START;
    p.x = (p.side_x == KHDAYS_HUD_AT_END ? w - box->x1 : box->x0) / unit;
    p.y = (p.side_y == KHDAYS_HUD_AT_END ? h - box->y1 : box->y0) / unit;
    p.w = (box->x1 - box->x0) / unit;
    p.h = (box->y1 - box->y0) / unit;
    return p;
}

void khdays_editor_control_box(const KhdaysHudPlace *p, float bw, float bh, float w, float h, float unit, KhdaysHudBox *out)
{
    if (p->w > 0.0f && p->h > 0.0f) {
        bw = p->w * unit;
        bh = p->h * unit;
    }
    out->x0 = p->side_x == KHDAYS_HUD_AT_END ? w - p->x * unit - bw : p->x * unit;
    out->y0 = p->side_y == KHDAYS_HUD_AT_END ? h - p->y * unit - bh : p->y * unit;
    out->x0 = kept_inside(out->x0, bw, 0.0f, w);
    out->y0 = kept_inside(out->y0, bh, 0.0f, h);
    out->x1 = out->x0 + bw;
    out->y1 = out->y0 + bh;
}
