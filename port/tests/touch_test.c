/* khdays-touch-test: the on-screen controls on a tablet's and a phone's screen, layouts A and C: each control
 * as large in points everywhere, none on another, the HUD's groups clear of them with the party's four members
 * too; what fingers hold; a group's place kept from its side in a picture of another shape. */
#include "../host/app/profile.h"
#include "../host/touch/touch_input.h"
#include "../host/touch/touch_layout.h"
#include "../ui/layout_editor.h"
#include "../ui/situation.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

typedef struct Device {
    const char *name;
    float w, h, density;
    int columns;
} Device;

static const Device devices[] = {
    {"Tab S8 Ultra", 2960.0f, 1848.0f, 2.25f, 308},
    {"a 20:9 phone", 2400.0f, 1080.0f, 2.625f, 426},
    {"16:9, the Odin's", 1920.0f, 1080.0f, 2.0f, 342},
};

static int overlap(const KhdaysTouchBox *a, const KhdaysTouchBox *b)
{
    return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

/* The group's piece in the window at the layout's place: its home box in dots moved, then in pixels. */
static KhdaysTouchBox piece(const KhdaysTouchLayout *l, int group, int columns, float x0, float y0, float x1, float y1)
{
    float dx, dy;
    KhdaysTouchBox b;
    khdays_hud_layout_set(l->hud);
    khdays_hud_layout_offset(group, columns, &dx, &dy);
    b.x0 = l->dots_x + (x0 + dx) * l->dot;
    b.x1 = l->dots_x + (x1 + dx) * l->dot;
    b.y0 = l->dots_y + (y0 + dy) * l->dot;
    b.y1 = l->dots_y + (y1 + dy) * l->dot;
    return b;
}

static void test_layouts(void)
{
    for (size_t d = 0; d < sizeof(devices) / sizeof(devices[0]); ++d) {
        for (int layout = KHDAYS_TOUCH_LAYOUT_A; layout <= KHDAYS_TOUCH_LAYOUT_C; ++layout) {
            const Device *e = &devices[d];
            KhdaysTouchScreen s;
            KhdaysTouchLayout l;
            memset(&s, 0, sizeof(s));
            s.w = e->w;
            s.h = e->h;
            s.density = e->density;
            s.size = 1.0f;
            s.columns = e->columns;
            s.hud = 1.0f;
            khdays_touch_layout_make(layout, &s, &l);
            /* as large under the thumb on every screen: 154 points */
            CHECK(fabsf((l.box[KHDAYS_TOUCH_FACE].x1 - l.box[KHDAYS_TOUCH_FACE].x0) / e->density - 154.0f) < 0.5f);
            for (int a = 0; a < KHDAYS_TOUCH_CONTROLS; ++a) {
                const KhdaysTouchBox *b = &l.box[a];
                CHECK(b->x0 >= 0.0f && b->y0 >= 0.0f && b->x1 <= e->w && b->y1 <= e->h);
                for (int c = a + 1; c < KHDAYS_TOUCH_CONTROLS; ++c) {
                    const int same_place = a == KHDAYS_TOUCH_STICK && c == KHDAYS_TOUCH_DPAD;
                    /* the lock-on lies over the cluster's empty corner */
                    const int in_corner = a == KHDAYS_TOUCH_FACE && c == KHDAYS_TOUCH_LOCK;
                    if (!same_place && !in_corner && overlap(b, &l.box[c])) {
                        fprintf(stderr, "%s, layout %d: controls %d and %d overlap\n", e->name, layout, a, c);
                        ++failures;
                    }
                }
            }
            {
                const float c = (float)e->columns;
                /* the deck's own rows; the party with four members (its 127 rows from y 64 at 0.767, the portrait's
                 * and the gauges' columns in its right part); the map and the mission's panel */
                const KhdaysTouchBox deck = piece(&l, KHDAYS_HUD_DECK, e->columns, 5.0f, 139.4f, 77.5f, 186.0f);
                const KhdaysTouchBox party = piece(&l, KHDAYS_HUD_PARTY, e->columns, c - 103.1f, 88.6f, c - 5.0f, 186.0f);
                const KhdaysTouchBox map = piece(&l, KHDAYS_HUD_MAP, e->columns, c - 67.0f, 5.0f, c - 5.0f, 70.0f);
                const KhdaysTouchBox panel = piece(&l, KHDAYS_HUD_MISSION, e->columns, 5.0f, 5.0f, 129.0f, 30.0f);
                const KhdaysTouchBox count = piece(&l, KHDAYS_HUD_COUNTDOWN, e->columns, c / 2.0f - 23.0f, 5.0f, c / 2.0f + 23.0f, 18.0f);
                for (int k = 0; k < KHDAYS_TOUCH_CONTROLS; ++k) {
                    if (k == KHDAYS_TOUCH_DPAD) {
                        continue; /* in a menu: no HUD */
                    }
                    if (overlap(&deck, &l.box[k]) || overlap(&panel, &l.box[k]) || overlap(&map, &l.box[k]) ||
                        overlap(&count, &l.box[k])) {
                        fprintf(stderr, "%s, layout %d: a group on control %d\n", e->name, layout, k);
                        ++failures;
                    }
                    /* the party against the lower controls (its rows of allies, up there, are small and see-through) */
                    if (overlap(&party, &l.box[k]) && (k == KHDAYS_TOUCH_FACE || k == KHDAYS_TOUCH_R)) {
                        fprintf(stderr, "%s, layout %d: the party on control %d\n", e->name, layout, k);
                        ++failures;
                    }
                }
                CHECK(!overlap(&deck, &party));
                CHECK(deck.y0 >= panel.y1 - 0.5f);
            }
        }
    }
    khdays_hud_layout_set(NULL);
}

static KhdaysTouchInput made(int mode)
{
    KhdaysTouchInput t;
    KhdaysTouchScreen s;
    memset(&t, 0, sizeof(t));
    memset(&s, 0, sizeof(s));
    s.w = 2960.0f;
    s.h = 1848.0f;
    s.density = 2.25f;
    s.size = 1.0f;
    s.columns = 308;
    s.hud = 1.0f;
    khdays_touch_layout_make(KHDAYS_TOUCH_LAYOUT_C, &s, &t.layout);
    t.stick_radius = 140.0f;
    t.stick_zone = s.w / 2.0f;
    t.tap_travel = 60.0f;
    t.mode = mode;
    return t;
}

#define BIT(a) (1u << (a))

static void test_fingers(void)
{
    {
        KhdaysTouchInput t = made(KHDAYS_TOUCH_MODE_FIELD);
        float ax, ay, bx, by, r;
        khdays_touch_face_button(&t.layout.box[KHDAYS_TOUCH_FACE], 0, &ax, &ay, &r);
        khdays_touch_face_button(&t.layout.box[KHDAYS_TOUCH_FACE], 1, &bx, &by, &r);
        /* a stick and a button at once */
        khdays_touch_input_down(&t, 1, 300.0f, 1500.0f);
        khdays_touch_input_move(&t, 1, 440.0f, 1500.0f);
        CHECK(t.stick_held && fabsf(t.stick_x - 1.0f) < 0.01f);
        khdays_touch_input_down(&t, 2, ax, ay);
        CHECK(t.buttons == BIT(KHDAYS_ACT_A));
        /* the thumb slides from A to B without lifting */
        khdays_touch_input_move(&t, 2, bx, by);
        CHECK(t.buttons == BIT(KHDAYS_ACT_B));
        /* past the stick's edge its middle follows: back a little answers at once */
        khdays_touch_input_move(&t, 1, 700.0f, 1500.0f);
        khdays_touch_input_move(&t, 1, 660.0f, 1500.0f);
        CHECK(t.stick_x > 0.6f && t.stick_x < 0.8f);
        khdays_touch_input_up(&t, 1);
        khdays_touch_input_up(&t, 2);
        CHECK(!t.stick_held && t.buttons == 0);
        /* the camera, on the right */
        khdays_touch_input_down(&t, 3, 1700.0f, 900.0f);
        khdays_touch_input_move(&t, 3, 1800.0f, 880.0f);
        CHECK(t.camera_x == 100.0 && t.camera_y == -20.0);
        khdays_touch_input_up(&t, 3);
        /* L with a face button: a shortcut */
        {
            const KhdaysTouchBox *l = &t.layout.box[KHDAYS_TOUCH_L];
            khdays_touch_input_down(&t, 4, (l->x0 + l->x1) / 2.0f, (l->y0 + l->y1) / 2.0f);
            khdays_touch_input_down(&t, 5, ax, ay);
            CHECK(t.buttons == (BIT(KHDAYS_ACT_L) | BIT(KHDAYS_ACT_A)));
            khdays_touch_input_up(&t, 4);
            khdays_touch_input_up(&t, 5);
        }
        /* the deck's rows take a tap before the stick does */
        t.deck_rows[1].x0 = 500.0f;
        t.deck_rows[1].x1 = 900.0f;
        t.deck_rows[1].y0 = 1600.0f;
        t.deck_rows[1].y1 = 1700.0f;
        khdays_touch_input_down(&t, 6, 600.0f, 1650.0f);
        CHECK(!t.stick_held);
        khdays_touch_input_up(&t, 6);
        CHECK(t.deck_serial == 1 && t.deck_row == 1);
        /* a finger the system takes back taps nothing */
        khdays_touch_input_down(&t, 8, 600.0f, 1650.0f);
        khdays_touch_input_cancel(&t, 8);
        CHECK(t.deck_serial == 1);
        /* a mode changing lets everything go */
        khdays_touch_input_down(&t, 7, ax, ay);
        khdays_touch_input_mode(&t, KHDAYS_TOUCH_MODE_NONE);
        CHECK(t.buttons == 0);
        CHECK(!khdays_touch_input_wants(&t, ax, ay));
    }
    {
        /* an event: a tap is A as it ends, a drag a direction and no A */
        KhdaysTouchInput t = made(KHDAYS_TOUCH_MODE_TALK);
        khdays_touch_input_down(&t, 1, 1000.0f, 1000.0f);
        CHECK(t.buttons == 0);
        khdays_touch_input_up(&t, 1);
        CHECK(t.tap_pulse && t.buttons == BIT(KHDAYS_ACT_A));
        t.tap_pulse = 0;
        t.buttons = 0;
        khdays_touch_input_down(&t, 2, 1000.0f, 1000.0f);
        khdays_touch_input_move(&t, 2, 1000.0f, 1100.0f);
        CHECK(t.buttons == BIT(KHDAYS_ACT_DOWN));
        khdays_touch_input_up(&t, 2);
        CHECK(!t.tap_pulse && t.buttons == 0);
    }
    {
        /* a menu: what is not a control is the picture's */
        KhdaysTouchInput t = made(KHDAYS_TOUCH_MODE_MENU);
        const KhdaysTouchBox *d = &t.layout.box[KHDAYS_TOUCH_DPAD];
        CHECK(!khdays_touch_input_wants(&t, 1480.0f, 900.0f));
        CHECK(khdays_touch_input_wants(&t, d->x0 + 5.0f, (d->y0 + d->y1) / 2.0f));
        khdays_touch_input_down(&t, 1, d->x1 - 5.0f, (d->y0 + d->y1) / 2.0f);
        CHECK(t.buttons == BIT(KHDAYS_ACT_RIGHT));
        khdays_touch_input_move(&t, 1, (d->x0 + d->x1) / 2.0f, d->y0 + 5.0f);
        CHECK(t.buttons == BIT(KHDAYS_ACT_UP));
    }
}

/* No screen of the game leaves a touch screen without controls; a display of the lower screen's own takes touches. */
static void test_needs(void)
{
    static const int menus[] = {KHDAYS_SIT_TITLE, KHDAYS_SIT_LOAD_MENU, KHDAYS_SIT_SAVE_MENU, KHDAYS_SIT_CAMP_MENU,
                                KHDAYS_SIT_PAUSE, KHDAYS_SIT_TUTORIAL, KHDAYS_SIT_SHOP, KHDAYS_SIT_MISSION_REVIEW,
                                KHDAYS_SIT_GAME_OVER, KHDAYS_SIT_WORLD_SELECTOR, KHDAYS_SIT_UNKNOWN};
    const KhdaysTouchSituation play = {0, 0, KHDAYS_SIT_FIELD, 1}, event = {0, 0, KHDAYS_SIT_FIELD, 0};
    const KhdaysTouchSituation movie = {0, 1, KHDAYS_SIT_FIELD, 1};
    for (int scene = 0; scene < KHDAYS_SITUATIONS; ++scene) {
        for (int hud = 0; hud < 2; ++hud) {
            KhdaysTouchSituation s = {0, 0, scene, hud};
            CHECK(khdays_touch_needs_mode(&s) != KHDAYS_TOUCH_MODE_NONE);
            s.port_menu = 1;
            CHECK(khdays_touch_needs_mode(&s) == KHDAYS_TOUCH_MODE_NONE);
        }
    }
    for (size_t i = 0; i < sizeof(menus) / sizeof(menus[0]); ++i) {
        const KhdaysTouchSituation s = {0, 0, menus[i], 0};
        CHECK(khdays_touch_needs_mode(&s) == KHDAYS_TOUCH_MODE_MENU);
    }
    CHECK(khdays_touch_needs_mode(&play) == KHDAYS_TOUCH_MODE_FIELD);
    CHECK(khdays_touch_needs_mode(&event) == KHDAYS_TOUCH_MODE_TALK);
    CHECK(khdays_touch_needs_mode(&movie) == KHDAYS_TOUCH_MODE_TALK);
    CHECK(khdays_touch_needs_window(0, 0) && khdays_touch_needs_window(1, 0) && khdays_touch_needs_window(1, 1));
    CHECK(!khdays_touch_needs_window(0, 1));
}

/* What the editor outlines: the drawn part of a group, without the blank its shapes keep. */
static void test_visible(void)
{
    KhdaysHudBox b;
    khdays_hud_layout_set(NULL);
    khdays_hud_layout_begin();
    khdays_hud_layout_note(KHDAYS_HUD_DECK, 5.0f, 96.0f, 98.3f, 186.0f, 0.8333f);
    khdays_hud_layout_note(KHDAYS_HUD_PARTY, 240.0f, 104.0f, 338.0f, 186.0f, 0.7667f);
    khdays_hud_layout_note(KHDAYS_HUD_MISSION, 5.0f, 5.0f, 129.0f, 25.0f, 0.5f);
    khdays_hud_layout_end();
    CHECK(khdays_hud_layout_visible(KHDAYS_HUD_DECK, 342, &b));
    CHECK(fabsf(b.y0 - (96.0f + 52.0f * 0.8333f)) < 0.01f && b.y1 == 186.0f && b.x0 == 5.0f && b.x1 < 98.3f);
    CHECK(khdays_hud_layout_visible(KHDAYS_HUD_PARTY, 342, &b));
    CHECK(b.y0 > 104.0f && b.y1 == 186.0f && b.x0 > 240.0f);
    CHECK(khdays_hud_layout_visible(KHDAYS_HUD_MISSION, 342, &b));
    CHECK(b.x0 == 5.0f && b.y0 == 5.0f && b.x1 == 129.0f && b.y1 == 25.0f); /* no blank kept */
    khdays_hud_layout_begin();
    khdays_hud_layout_note(KHDAYS_HUD_PARTY, 240.0f, 89.0f, 338.0f, 186.0f, 0.7667f); /* four members */
    khdays_hud_layout_end();
    CHECK(khdays_hud_layout_visible(KHDAYS_HUD_PARTY, 342, &b));
    CHECK(b.y0 == 89.0f);
}

static void test_places(void)
{
    KhdaysHudPlace places[KHDAYS_HUD_GROUPS];
    float dx, dy;
    CHECK(khdays_hud_group_named("deck") == KHDAYS_HUD_DECK && khdays_hud_group_named("nothing") == -1);
    khdays_hud_layout_offset(KHDAYS_HUD_DECK, 342, &dx, &dy);
    CHECK(dx == 0.0f && dy == 0.0f); /* no layout */
    memset(places, 0, sizeof(places));
    places[KHDAYS_HUD_PARTY] = khdays_hud_place_moved(KHDAYS_HUD_PARTY, -30.0f, -10.0f);
    khdays_hud_layout_set(places);
    khdays_hud_layout_offset(KHDAYS_HUD_PARTY, 342, &dx, &dy);
    CHECK(fabsf(dx + 30.0f) < 0.01f && fabsf(dy + 10.0f) < 0.01f);
    khdays_hud_layout_offset(KHDAYS_HUD_PARTY, 426, &dx, &dy); /* the same from the right side */
    CHECK(fabsf(dx + 30.0f) < 0.01f);
    khdays_hud_layout_offset(KHDAYS_HUD_DECK, 342, &dx, &dy); /* a group not set stays */
    CHECK(dx == 0.0f && dy == 0.0f);
    {
        /* the map dragged to the lower left: measured from there, it stays that far from them in another shape */
        const KhdaysHudBox box = {10.0f, 120.0f, 72.0f, 185.0f};
        const KhdaysHudPlace moved = khdays_hud_place_moved(KHDAYS_HUD_MAP, -270.0f, 115.0f);
        const KhdaysHudPlace kept = khdays_hud_place_nearest(&moved, &box, KHDAYS_HUD_MAP, 342);
        CHECK(kept.side_x == KHDAYS_HUD_AT_START && kept.side_y == KHDAYS_HUD_AT_END);
        memset(places, 0, sizeof(places));
        places[KHDAYS_HUD_MAP] = kept;
        khdays_hud_layout_set(places);
        khdays_hud_layout_offset(KHDAYS_HUD_MAP, 342, &dx, &dy);
        CHECK(fabsf(dx + 270.0f) < 0.01f && fabsf(dy - 115.0f) < 0.01f);
        khdays_hud_layout_offset(KHDAYS_HUD_MAP, 426, &dx, &dy);
        CHECK(fabsf(dx + 354.0f) < 0.01f); /* its left edge where it was, the picture wider */
    }
    khdays_hud_layout_set(NULL);
}

static void test_editor(void)
{
    KhdaysLayoutEditor e;
    memset(&e, 0, sizeof(e));
    e.held = -1;
    e.area.x1 = 1000.0f;
    e.area.y1 = 500.0f;
    e.grid = 10.0f;
    e.snap = 1;
    e.count = 2;
    e.item[0].id = 0;
    e.item[0].box = (KhdaysHudBox){100.0f, 100.0f, 300.0f, 300.0f};
    e.item[1].id = 1;
    e.item[1].box = (KhdaysHudBox){150.0f, 150.0f, 200.0f, 200.0f}; /* inside the first: still reachable */
    CHECK(khdays_editor_item_at(&e, 160.0f, 160.0f) == 1);
    CHECK(khdays_editor_item_at(&e, 110.0f, 110.0f) == 0);
    CHECK(khdays_editor_item_at(&e, 5.0f, 5.0f) == -1);
    CHECK(khdays_editor_press(&e, 5.0f, 5.0f) == -1 && !khdays_editor_drag(&e, 50.0f, 50.0f));
    CHECK(khdays_editor_press(&e, 110.0f, 110.0f) == 0);
    /* follows the point where it was held exactly, not by the grid's steps; settles on the grid when let go */
    CHECK(khdays_editor_drag(&e, 214.0f, 123.0f));
    CHECK(e.item[0].box.x0 == 204.0f && e.item[0].box.y0 == 113.0f && e.item[0].box.x1 == 404.0f);
    CHECK(khdays_editor_drag(&e, 215.0f, 123.0f) && e.item[0].box.x0 == 205.0f);
    CHECK(khdays_editor_release(&e) && e.held == -1);
    CHECK(e.item[0].box.x0 == 210.0f && e.item[0].box.y0 == 110.0f && e.item[0].box.x1 == 410.0f);
    CHECK(khdays_editor_press(&e, 300.0f, 300.0f) == 0);
    CHECK(!khdays_editor_release(&e)); /* already on it */
    CHECK(khdays_editor_press(&e, 300.0f, 300.0f) == 0);
    /* stays wholly inside */
    khdays_editor_drag(&e, 5000.0f, 5000.0f);
    CHECK(e.item[0].box.x1 == 1000.0f && e.item[0].box.y1 == 500.0f);
    khdays_editor_drag(&e, -500.0f, -500.0f);
    CHECK(e.item[0].box.x0 == 0.0f && e.item[0].box.y0 == 0.0f);
    khdays_editor_release(&e);
    CHECK(!khdays_editor_drag(&e, 10.0f, 10.0f));
    {
        /* a piece lifted from a picture with it and one without: only what differs */
        unsigned char with[8] = {200, 10, 10, 255, 50, 50, 50, 255}, without[8] = {10, 10, 10, 255, 52, 50, 50, 255};
        khdays_editor_cutout(with, 8, without, 8, 2, 1);
        CHECK(with[0] == 200 && with[3] == 255 && with[7] == 0);
    }
    /* what the outline leaves out stays on the screen too */
    e.item[1].room = (KhdaysHudBox){0.0f, 40.0f, 0.0f, 0.0f};
    CHECK(khdays_editor_press(&e, 160.0f, 160.0f) == 1);
    khdays_editor_drag(&e, 160.0f, 0.0f);
    CHECK(e.item[1].box.y0 == 40.0f);
    khdays_editor_release(&e);
    CHECK(e.item[1].box.y0 == 40.0f);
    {
        /* a control's place, from its nearest sides, back to the same box; on a wider window as far from the right */
        const KhdaysHudBox b = {800.0f, 400.0f, 900.0f, 480.0f};
        const KhdaysHudPlace p = khdays_editor_control_place(&b, 1000.0f, 500.0f, 2.0f);
        KhdaysHudBox back;
        CHECK(p.side_x == KHDAYS_HUD_AT_END && p.side_y == KHDAYS_HUD_AT_END && p.x == 50.0f && p.y == 10.0f);
        khdays_editor_control_box(&p, 100.0f, 80.0f, 1000.0f, 500.0f, 2.0f, &back);
        CHECK(back.x0 == 800.0f && back.y1 == 480.0f);
        khdays_editor_control_box(&p, 100.0f, 80.0f, 2000.0f, 500.0f, 2.0f, &back);
        CHECK(back.x1 == 1900.0f);
        /* ... and on a smaller one it stays on the screen */
        khdays_editor_control_box(&p, 100.0f, 80.0f, 90.0f, 60.0f, 2.0f, &back);
        CHECK(back.x0 == 0.0f && back.y0 == 0.0f);
    }
    {
        /* a group dragged: the offset it then gives is the drag */
        KhdaysHudPlace places[KHDAYS_HUD_GROUPS];
        const KhdaysHudBox from = {280.0f, 5.0f, 337.0f, 70.0f}, to = {10.0f, 120.0f, 67.0f, 185.0f};
        float dx, dy;
        const KhdaysHudPlace home = khdays_hud_place_home(KHDAYS_HUD_MAP);
        memset(places, 0, sizeof(places));
        places[KHDAYS_HUD_MAP] = khdays_editor_place(&home, &from, &to, KHDAYS_HUD_MAP, 342);
        CHECK(places[KHDAYS_HUD_MAP].side_x == KHDAYS_HUD_AT_START && places[KHDAYS_HUD_MAP].side_y == KHDAYS_HUD_AT_END);
        khdays_hud_layout_set(places);
        khdays_hud_layout_offset(KHDAYS_HUD_MAP, 342, &dx, &dy);
        CHECK(fabsf(dx + 270.0f) < 0.01f && fabsf(dy - 115.0f) < 0.01f);
        khdays_hud_layout_set(NULL);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    test_layouts();
    test_fingers();
    test_places();
    test_editor();
    test_needs();
    test_visible();
    if (failures != 0) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    puts("touch controls: ok");
    return 0;
}
