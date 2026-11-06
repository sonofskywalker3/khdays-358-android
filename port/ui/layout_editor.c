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
    e->made_way = 0;
    if (e->held >= 0) {
        e->grab_x = x - e->item[e->held].box.x0;
        e->grab_y = y - e->item[e->held].box.y0;
        e->from_x = e->item[e->held].box.x0;
        e->from_y = e->item[e->held].box.y0;
    }
    return e->held;
}

#define EDITOR_SEARCH_STEP 16.0f /* pixels between the places tried when no grid is set */

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
    const KhdaysHudBox *o = &it->only;
    KhdaysHudBox a = e->area;
    if (o->x1 > o->x0 && o->y1 > o->y0) {
        a.x0 = o->x0 > a.x0 ? o->x0 : a.x0;
        a.y0 = o->y0 > a.y0 ? o->y0 : a.y0;
        a.x1 = o->x1 < a.x1 ? o->x1 : a.x1;
        a.y1 = o->y1 < a.y1 ? o->y1 : a.y1;
    }
    *x0 = kept_inside(*x0 - r->x0, w + r->x0 + r->x1, a.x0, a.x1) + r->x0;
    *y0 = kept_inside(*y0 - r->y0, h + r->y0 + r->y1, a.y0, a.y1) + r->y0;
}

/* All an item takes at a place: itself and what it sometimes draws past its sides. */
static KhdaysHudBox item_whole(const KhdaysEditorItem *it, float x0, float y0, float w, float h)
{
    KhdaysHudBox b;
    b.x0 = x0 - it->room.x0;
    b.y0 = y0 - it->room.y0;
    b.x1 = x0 + w + it->room.x1;
    b.y1 = y0 + h + it->room.y1;
    return b;
}

static int boxes_meet(const KhdaysHudBox *a, const KhdaysHudBox *b)
{
    return a->x0 < b->x1 && a->x1 > b->x0 && a->y0 < b->y1 && a->y1 > b->y0;
}

static KhdaysHudBox item_whole_now(const KhdaysEditorItem *it)
{
    return item_whole(it, it->box.x0, it->box.y0, it->box.x1 - it->box.x0, it->box.y1 - it->box.y0);
}

/* Whether an item at a place is on the area and on none of the others of its kind. */
static int item_free_at(const KhdaysLayoutEditor *e, int index, float *x0, float *y0)
{
    const KhdaysEditorItem *it = &e->item[index];
    const float w = it->box.x1 - it->box.x0, h = it->box.y1 - it->box.y0;
    KhdaysHudBox at;
    item_inside(e, it, w, h, x0, y0);
    at = item_whole(it, *x0, *y0, w, h);
    for (int i = 0; i < e->count; ++i) {
        if (i != index && e->item[i].kind == it->kind) {
            const KhdaysHudBox other = item_whole_now(&e->item[i]);
            if (boxes_meet(&at, &other)) {
                return 0;
            }
        }
    }
    return 1;
}

static void item_place(KhdaysEditorItem *it, float x0, float y0)
{
    const float w = it->box.x1 - it->box.x0, h = it->box.y1 - it->box.y0;
    it->box.x0 = x0;
    it->box.y0 = y0;
    it->box.x1 = x0 + w;
    it->box.y1 = y0 + h;
}

/* An item moved out from under another: to `prefer` if it fits there, else to the nearest free place. */
static int item_make_way(KhdaysLayoutEditor *e, int index, float prefer_x, float prefer_y)
{
    const float step = e->grid > 0.0f ? e->grid : EDITOR_SEARCH_STEP;
    const float from_x = e->item[index].box.x0, from_y = e->item[index].box.y0;
    float best_x = 0.0f, best_y = 0.0f, best = -1.0f;
    const KhdaysEditorItem *me = &e->item[index];
    const float w = me->box.x1 - me->box.x0, h = me->box.y1 - me->box.y0;
    float xs[2 * KHDAYS_EDITOR_ITEMS + 1], ys[2 * KHDAYS_EDITOR_ITEMS + 1];
    int edges = 0;
    float x = prefer_x, y = prefer_y;
    if (item_free_at(e, index, &x, &y)) {
        item_place(&e->item[index], x, y);
        return 1;
    }
    /* touching another's side: a gap of exactly its size lies on none of the steps */
    xs[edges] = from_x;
    ys[edges++] = from_y;
    for (int i = 0; i < e->count; ++i) {
        if (i != index && e->item[i].kind == me->kind) {
            const KhdaysHudBox o = item_whole_now(&e->item[i]);
            xs[edges] = o.x0 - w - me->room.x1;
            ys[edges++] = o.y0 - h - me->room.y1;
            xs[edges] = o.x1 + me->room.x0;
            ys[edges++] = o.y1 + me->room.y0;
        }
    }
    for (float ty = e->area.y0; ty < e->area.y1 + edges * step; ty += step) {
        for (float tx = e->area.x0; tx < e->area.x1 + edges * step; tx += step) {
            /* past the area's end: the sides' places in place of a step */
            const int ex = tx < e->area.x1 ? -1 : (int)((tx - e->area.x1) / step);
            const int ey = ty < e->area.y1 ? -1 : (int)((ty - e->area.y1) / step);
            x = ex < 0 ? tx : xs[ex < edges ? ex : edges - 1];
            y = ey < 0 ? ty : ys[ey < edges ? ey : edges - 1];
            if (item_free_at(e, index, &x, &y)) {
                const float d = (x - from_x) * (x - from_x) + (y - from_y) * (y - from_y);
                if (best < 0.0f || d < best) {
                    best = d;
                    best_x = x;
                    best_y = y;
                }
            }
        }
    }
    if (best < 0.0f) {
        return 0;
    }
    item_place(&e->item[index], best_x, best_y);
    return 1;
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
    KhdaysEditorItem saved[KHDAYS_EDITOR_ITEMS];
    KhdaysEditorItem *it;
    KhdaysHudBox was, whole;
    float x0, y0;
    const int held = e->held;
    e->held = -1;
    e->made_way = 0;
    if (held < 0) {
        return 0;
    }
    it = &e->item[held];
    was = it->box;
    if (e->snap && e->grid > 0.0f) {
        x0 = roundf(it->box.x0 / e->grid) * e->grid;
        y0 = roundf(it->box.y0 / e->grid) * e->grid;
        item_inside(e, it, it->box.x1 - it->box.x0, it->box.y1 - it->box.y0, &x0, &y0);
        item_place(it, x0, y0);
    }
    memcpy(saved, e->item, sizeof(saved));
    whole = item_whole_now(it);
    for (int i = 0; i < e->count; ++i) {
        if (i != held && e->item[i].kind == it->kind) {
            const KhdaysHudBox other = item_whole_now(&e->item[i]);
            if (boxes_meet(&whole, &other)) {
                if (!item_make_way(e, i, e->from_x, e->from_y)) {
                    /* no room for it anywhere: the piece goes back, and nothing else moves */
                    memcpy(e->item, saved, sizeof(saved));
                    item_place(it, e->from_x, e->from_y);
                    e->made_way = 0;
                    return 1;
                }
                e->made_way |= 1u << i;
            }
        }
    }
    return memcmp(&was, &it->box, sizeof(was)) != 0;
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
