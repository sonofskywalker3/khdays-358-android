/* hud_layout.h. The layout is written rarely, from the window's thread, and read for every picture: two
 * copies and an index, so a reader never sees a half-written one. */
#include "hud_layout.h"

#include <SDL3/SDL.h>
#include <string.h>

#define PICTURE_ROWS 192.0f

static const struct {
    const char *name;
    int side_x, side_y; /* the corner of the picture the composition measures it from */
} groups[KHDAYS_HUD_GROUPS] = {
    [KHDAYS_HUD_MISSION] = {"mission", KHDAYS_HUD_AT_START, KHDAYS_HUD_AT_START},
    [KHDAYS_HUD_COUNTER] = {"counter", KHDAYS_HUD_AT_START, KHDAYS_HUD_AT_START},
    [KHDAYS_HUD_MAP] = {"map", KHDAYS_HUD_AT_END, KHDAYS_HUD_AT_START},
    [KHDAYS_HUD_COUNTDOWN] = {"countdown", KHDAYS_HUD_AT_MIDDLE, KHDAYS_HUD_AT_START},
    [KHDAYS_HUD_DECK] = {"deck", KHDAYS_HUD_AT_START, KHDAYS_HUD_AT_END},
    [KHDAYS_HUD_PARTY] = {"party", KHDAYS_HUD_AT_END, KHDAYS_HUD_AT_END},
};

static KhdaysHudPlace layouts[2][KHDAYS_HUD_GROUPS];
static SDL_AtomicInt shown; /* one more than the copy in force, 0 none */

typedef struct Seen {
    int any;
    KhdaysHudBox box;
    float scale;
} Seen;
static Seen seen[2][KHDAYS_HUD_GROUPS]; /* the last picture's boxes, in the copy `seen_now` names */
static Seen making[KHDAYS_HUD_GROUPS];
static SDL_AtomicInt seen_now;

int khdays_hud_group_named(const char *name)
{
    for (int g = 0; g < KHDAYS_HUD_GROUPS; ++g) {
        if (strcmp(groups[g].name, name) == 0) {
            return g;
        }
    }
    return -1;
}

const char *khdays_hud_group_name(int group)
{
    return group >= 0 && group < KHDAYS_HUD_GROUPS ? groups[group].name : "";
}

KhdaysHudPlace khdays_hud_place_home(int group)
{
    KhdaysHudPlace p;
    memset(&p, 0, sizeof(p));
    p.set = 1;
    p.side_x = groups[group].side_x;
    p.side_y = groups[group].side_y;
    return p;
}

KhdaysHudPlace khdays_hud_place_moved(int group, float dx, float dy)
{
    KhdaysHudPlace p = khdays_hud_place_home(group);
    p.x = p.side_x == KHDAYS_HUD_AT_END ? -dx : dx;
    p.y = p.side_y == KHDAYS_HUD_AT_END ? -dy : dy;
    return p;
}

/* The point's distance across the picture, from its left or top. */
static float across(int side, float value, float length)
{
    return side == KHDAYS_HUD_AT_END ? length - value : side == KHDAYS_HUD_AT_MIDDLE ? length / 2.0f + value : value;
}

KhdaysHudPlace khdays_hud_place_nearest(const KhdaysHudPlace *place, const KhdaysHudBox *box, int group, int columns)
{
    const float c = (float)columns;
    const float x = across(place->side_x, place->x, c), y = across(place->side_y, place->y, PICTURE_ROWS);
    KhdaysHudPlace p = khdays_hud_place_home(group);
    p.side_x = (box->x0 + box->x1) / 2.0f >= c / 2.0f ? KHDAYS_HUD_AT_END : KHDAYS_HUD_AT_START;
    p.side_y = (box->y0 + box->y1) / 2.0f >= PICTURE_ROWS / 2.0f ? KHDAYS_HUD_AT_END : KHDAYS_HUD_AT_START;
    p.x = p.side_x == KHDAYS_HUD_AT_END ? c - x : x;
    p.y = p.side_y == KHDAYS_HUD_AT_END ? PICTURE_ROWS - y : y;
    return p;
}

void khdays_hud_layout_set(const KhdaysHudPlace *places)
{
    int next;
    if (places == NULL) {
        SDL_SetAtomicInt(&shown, 0);
        return;
    }
    next = SDL_GetAtomicInt(&shown) == 1 ? 2 : 1;
    memcpy(layouts[next - 1], places, sizeof(layouts[0]));
    SDL_SetAtomicInt(&shown, next);
}

void khdays_hud_layout_offset(int group, int columns, float *dx, float *dy)
{
    const int now = SDL_GetAtomicInt(&shown);
    *dx = *dy = 0.0f;
    if (now != 0 && group >= 0 && group < KHDAYS_HUD_GROUPS && layouts[now - 1][group].set) {
        const KhdaysHudPlace *p = &layouts[now - 1][group];
        const float c = (float)columns;
        *dx = across(p->side_x, p->x, c) - across(groups[group].side_x, 0.0f, c);
        *dy = across(p->side_y, p->y, PICTURE_ROWS) - across(groups[group].side_y, 0.0f, PICTURE_ROWS);
    }
}

void khdays_hud_layout_begin(void)
{
    memset(making, 0, sizeof(making));
}

void khdays_hud_layout_note(int group, float x0, float y0, float x1, float y1, float scale)
{
    Seen *s = &making[group];
    if (!s->any) {
        s->any = 1;
        s->scale = scale;
        s->box.x0 = x0;
        s->box.y0 = y0;
        s->box.x1 = x1;
        s->box.y1 = y1;
        return;
    }
    s->box.x0 = x0 < s->box.x0 ? x0 : s->box.x0;
    s->box.y0 = y0 < s->box.y0 ? y0 : s->box.y0;
    s->box.x1 = x1 > s->box.x1 ? x1 : s->box.x1;
    s->box.y1 = y1 > s->box.y1 ? y1 : s->box.y1;
}

void khdays_hud_layout_end(void)
{
    const int next = !SDL_GetAtomicInt(&seen_now);
    memcpy(seen[next], seen[!next], sizeof(seen[0])); /* a group this picture lacks keeps its last box */
    for (int g = 0; g < KHDAYS_HUD_GROUPS; ++g) {
        if (making[g].any) {
            seen[next][g] = making[g];
        }
    }
    SDL_SetAtomicInt(&seen_now, next);
}

#define PARTY_TWO_ROWS 110.0f /* the party's shape is taller than this with four members */

static void moved_box(int group, int columns, KhdaysHudBox *box)
{
    float dx, dy;
    khdays_hud_layout_offset(group, columns, &dx, &dy);
    box->x0 += dx;
    box->x1 += dx;
    box->y0 += dy;
    box->y1 += dy;
}

int khdays_hud_layout_box(int group, int columns, KhdaysHudBox *box, float *scale)
{
    const Seen *s = &seen[SDL_GetAtomicInt(&seen_now)][group];
    if (!s->any) {
        return 0;
    }
    *box = s->box;
    *scale = s->scale;
    moved_box(group, columns, box);
    return 1;
}

int khdays_hud_layout_visible(int group, int columns, KhdaysHudBox *box)
{
    /* the blank in the shapes' rectangles, in the source's dots of the group's first shape, from each side */
    static const KhdaysHudBox blank[KHDAYS_HUD_GROUPS] = {
        [KHDAYS_HUD_MAP] = {6.0f, 0.0f, 3.0f, 39.0f},    /* the enemy's health, with one locked on, under the map */
        [KHDAYS_HUD_DECK] = {0.0f, 52.0f, 24.0f, 0.0f},  /* the reaction command, when offered, over the deck */
        [KHDAYS_HUD_PARTY] = {31.0f, 55.0f, 2.0f, 0.0f}, /* two members; four fill the rows */
    };
    float scale;
    KhdaysHudBox b, e;
    if (!khdays_hud_layout_box(group, columns, &b, &scale)) {
        return 0;
    }
    e = blank[group];
    if (group == KHDAYS_HUD_PARTY && (b.y1 - b.y0) / scale > PARTY_TWO_ROWS) {
        e.y0 = 0.0f;
    }
    box->x0 = b.x0 + e.x0 * scale;
    box->y0 = b.y0 + e.y0 * scale;
    box->x1 = b.x1 - e.x1 * scale;
    box->y1 = b.y1 - e.y1 * scale;
    return 1;
}

void khdays_hud_layout_box_expected(int group, int columns, float hud, KhdaysHudBox *box)
{
    /* inwards from the group's corner of the picture, at a factor of 1 */
    static const KhdaysHudBox expected[KHDAYS_HUD_GROUPS] = {
        [KHDAYS_HUD_MISSION] = {5.0f, 5.0f, 129.0f, 30.0f},
        [KHDAYS_HUD_COUNTER] = {0.0f, 29.0f, 72.0f, 86.0f},
        [KHDAYS_HUD_MAP] = {5.0f, 5.0f, 67.0f, 70.0f},
        [KHDAYS_HUD_COUNTDOWN] = {-23.0f, 5.0f, 23.0f, 18.0f},
        [KHDAYS_HUD_DECK] = {5.0f, 6.0f, 98.0f, 96.0f},
        [KHDAYS_HUD_PARTY] = {5.0f, 6.0f, 103.0f, 88.0f},
    };
    const KhdaysHudBox *e = &expected[group];
    const float c = (float)columns;
    if (groups[group].side_x == KHDAYS_HUD_AT_END) {
        box->x0 = c - e->x1 * hud;
        box->x1 = c - e->x0 * hud;
    } else if (groups[group].side_x == KHDAYS_HUD_AT_MIDDLE) {
        box->x0 = c / 2.0f + e->x0 * hud;
        box->x1 = c / 2.0f + e->x1 * hud;
    } else {
        box->x0 = e->x0 * hud;
        box->x1 = e->x1 * hud;
    }
    if (groups[group].side_y == KHDAYS_HUD_AT_END) {
        box->y0 = PICTURE_ROWS - e->y1 * hud;
        box->y1 = PICTURE_ROWS - e->y0 * hud;
    } else {
        box->y0 = e->y0 * hud;
        box->y1 = e->y1 * hud;
    }
    moved_box(group, columns, box);
}
