/* touch_layout.h. Sizes are in points at the default size; a HUD group's home is where
 * ui/compositions/port_days.txt has it at the HUD's factor. */
#include "touch_layout.h"
#include "../../ui/layout_editor.h"

#include <stddef.h>

#define ROWS 192.0f

#define EDGE 14.0f        /* the controls' distance from the window's edges */
#define GAP 16.0f         /* between controls */
#define STICK 126.0f      /* the stick's and the D-pad's box */
#define STICK_FOOT 33.0f  /* under them */
#define FACE 154.0f       /* the four buttons' box */
#define FACE_FOOT 12.0f   /* under it */
#define SHOULDER_W 73.0f  /* L, R */
#define SHOULDER_H 38.0f
#define LOCK 42.0f        /* the lock-on */
#define PILL_W 72.0f      /* Select, Start */
#define PILL_H 30.0f
#define GEAR 34.0f
#define PILL_GAP 8.0f
#define LOCK_INTO_C 14.0f /* layout C: its middle this far left of the cluster, over X's corner */
#define LOCK_LEFT_A 45.0f /* layout A: its middle this far left of the cluster, beside B */

/* port_days.txt's places, at a HUD factor of 1 */
#define HOME_EDGE 5.0f      /* the deck's and the party's distance from the side */
#define HOME_FOOT 6.0f      /* and from the bottom */
#define DECK_SHOWN_W 72.5f  /* the deck's own width, 87 source dots at 0.833 */
#define DECK_SHOWN_H 46.6f  /* its height without the reaction command above it */
#define PANEL_END 129.0f    /* the mission's panel's right end: 5 and 248 source dots at 0.5 */
#define MAP_W 67.0f         /* the map's distance from the right side with its margin */
#define COUNTDOWN_TOP 5.0f
#define PANEL_FOOT 30.0f    /* the panel's bottom */
#define COUNTER_FOOT 86.0f  /* the hearts' counter's: 29 and 86 source dots at 0.667 */
#define MARGIN_DOTS 2.0f    /* between a group and a control, in dots */

void khdays_touch_picture(const KhdaysTouchScreen *s, float *x, float *y, float *dot)
{
    const float across = s->w / (float)s->columns, down = s->h / ROWS;
    const float d = across < down ? across : down;
    *dot = d;
    *x = (s->w - d * (float)s->columns) / 2.0f;
    *y = (s->h - d * ROWS) / 2.0f;
}

static KhdaysTouchBox box_at(float x0, float y0, float w, float h)
{
    const KhdaysTouchBox b = {x0, y0, x0 + w, y0 + h};
    return b;
}

void khdays_touch_layout_make(int layout, const KhdaysTouchScreen *s, KhdaysTouchLayout *out)
{
    const float u = s->density * s->size; /* a control's point */
    const float edge = EDGE * s->density;
    const float f = s->hud, c = (float)s->columns;
    float ox, oy, dot;
    KhdaysTouchBox *b = out->box;
    khdays_touch_picture(s, &ox, &oy, &dot);
    out->dots_x = ox;
    out->dots_y = oy;
    out->dot = dot;

    b[KHDAYS_TOUCH_STICK] = box_at(edge, s->h - (STICK_FOOT * s->density) - STICK * u, STICK * u, STICK * u);
    b[KHDAYS_TOUCH_DPAD] = b[KHDAYS_TOUCH_STICK];
    b[KHDAYS_TOUCH_FACE] = box_at(s->w - edge - FACE * u, s->h - FACE_FOOT * s->density - FACE * u, FACE * u, FACE * u);
    b[KHDAYS_TOUCH_L] = box_at(edge, b[KHDAYS_TOUCH_STICK].y0 - (GAP + SHOULDER_H) * u, SHOULDER_W * u, SHOULDER_H * u);
    b[KHDAYS_TOUCH_R] = box_at(s->w - edge - SHOULDER_W * u, b[KHDAYS_TOUCH_FACE].y0 - (GAP + SHOULDER_H) * u,
                               SHOULDER_W * u, SHOULDER_H * u);
    if (layout == KHDAYS_TOUCH_LAYOUT_A) {
        b[KHDAYS_TOUCH_LOCK] = box_at(b[KHDAYS_TOUCH_FACE].x0 - (LOCK_LEFT_A + LOCK / 2.0f) * u,
                                      b[KHDAYS_TOUCH_FACE].y1 - LOCK * u, LOCK * u, LOCK * u);
    } else {
        b[KHDAYS_TOUCH_LOCK] = box_at(b[KHDAYS_TOUCH_FACE].x0 - (LOCK_INTO_C + LOCK / 2.0f) * u,
                                      b[KHDAYS_TOUCH_FACE].y0 - LOCK / 2.0f * u, LOCK * u, LOCK * u);
    }
    {
        /* Select, Start and the gear along the top, in the middle, clear of the mission's panel and the map */
        const float row = (2.0f * PILL_W + GEAR + 2.0f * PILL_GAP) * u;
        const float panel_end = ox + PANEL_END * f * dot + PILL_GAP * u;
        const float map_start = ox + (c - MAP_W * f) * dot - PILL_GAP * u;
        float x = (s->w - row) / 2.0f;
        x = x < panel_end ? panel_end : x;
        x = x + row > map_start ? map_start - row : x;
        b[KHDAYS_TOUCH_SELECT] = box_at(x, edge, PILL_W * u, PILL_H * u);
        b[KHDAYS_TOUCH_START] = box_at(x + (PILL_W + PILL_GAP) * u, edge, PILL_W * u, PILL_H * u);
        b[KHDAYS_TOUCH_GEAR] = box_at(x + (2.0f * PILL_W + 2.0f * PILL_GAP) * u, edge + (PILL_H - GEAR) / 2.0f * u,
                                      GEAR * u, GEAR * u);
    }

    for (int g = 0; g < KHDAYS_HUD_GROUPS; ++g) {
        out->hud[g] = khdays_hud_place_home(g);
    }
    {
        /* under the row of Select and Start, when they would cover it */
        const float under = (b[KHDAYS_TOUCH_START].y1 - oy) / dot + MARGIN_DOTS - COUNTDOWN_TOP * f;
        if (under > 0.0f) {
            out->hud[KHDAYS_HUD_COUNTDOWN] = khdays_hud_place_moved(KHDAYS_HUD_COUNTDOWN, 0.0f, under);
        }
    }
    if (layout == KHDAYS_TOUCH_LAYOUT_A) {
        /* the deck above L and the party above R, the hearts' counter beside the deck */
        const float deck_foot = (b[KHDAYS_TOUCH_L].y0 - oy) / dot - MARGIN_DOTS;
        const float party_foot = (b[KHDAYS_TOUCH_R].y0 - oy) / dot - MARGIN_DOTS;
        const float deck_home = ROWS - HOME_FOOT * f, party_home = ROWS - HOME_FOOT * f;
        const float highest = PANEL_FOOT * f + DECK_SHOWN_H * f; /* the deck's foot, its top under the panel */
        const float foot = deck_foot < highest ? highest : deck_foot;
        out->hud[KHDAYS_HUD_DECK] = khdays_hud_place_moved(KHDAYS_HUD_DECK, 0.0f, foot - deck_home);
        out->hud[KHDAYS_HUD_PARTY] = khdays_hud_place_moved(KHDAYS_HUD_PARTY, 0.0f, party_foot - party_home);
        if (foot - DECK_SHOWN_H * f < COUNTER_FOOT * f) {
            out->hud[KHDAYS_HUD_COUNTER] = khdays_hud_place_moved(KHDAYS_HUD_COUNTER, (DECK_SHOWN_W + MARGIN_DOTS) * f, 0.0f);
        }
    } else {
        /* the deck beside the stick and the party beside the buttons, both along the bottom */
        const float stick_end = (b[KHDAYS_TOUCH_STICK].x1 - ox) / dot + MARGIN_DOTS;
        const float face_start = (b[KHDAYS_TOUCH_FACE].x0 - ox) / dot - MARGIN_DOTS;
        const float deck_move = stick_end - HOME_EDGE * f;
        const float party_move = face_start - (c - HOME_EDGE * f);
        out->hud[KHDAYS_HUD_DECK] = khdays_hud_place_moved(KHDAYS_HUD_DECK, deck_move > 0.0f ? deck_move : 0.0f, 0.0f);
        out->hud[KHDAYS_HUD_PARTY] =
            khdays_hud_place_moved(KHDAYS_HUD_PARTY, party_move < 0.0f ? party_move : 0.0f, 0.0f);
    }
}

void khdays_touch_layout_custom(const KhdaysTouchScreen *s, const KhdaysHudPlace *hud, const KhdaysHudPlace *control,
                                KhdaysTouchLayout *out)
{
    khdays_touch_layout_make(KHDAYS_TOUCH_LAYOUT_C, s, out);
    for (int g = 0; g < KHDAYS_HUD_GROUPS; ++g) {
        out->hud[g] = hud != NULL && hud[g].set ? hud[g] : khdays_hud_place_home(g);
    }
    for (int i = 0; control != NULL && i < KHDAYS_TOUCH_CONTROLS; ++i) {
        if (control[i].set) {
            KhdaysTouchBox *b = &out->box[i];
            KhdaysHudBox moved;
            khdays_editor_control_box(&control[i], b->x1 - b->x0, b->y1 - b->y0, s->w, s->h, s->density, &moved);
            b->x0 = moved.x0;
            b->y0 = moved.y0;
            b->x1 = moved.x1;
            b->y1 = moved.y1;
        }
    }
}

int khdays_touch_in_box(const KhdaysTouchBox *b, float x, float y, float reach)
{
    return x >= b->x0 - reach && x < b->x1 + reach && y >= b->y0 - reach && y < b->y1 + reach;
}

void khdays_touch_face_button(const KhdaysTouchBox *cluster, int button, float *x, float *y, float *r)
{
    const float w = cluster->x1 - cluster->x0, h = cluster->y1 - cluster->y0;
    const float rx = w * 0.17f;
    const float cx = (cluster->x0 + cluster->x1) / 2.0f, cy = (cluster->y0 + cluster->y1) / 2.0f;
    const float away = h / 2.0f - rx;
    *r = rx;
    *x = button == 0 ? cx + away : button == 3 ? cx - away : cx;
    *y = button == 2 ? cy - away : button == 1 ? cy + away : cy;
}
