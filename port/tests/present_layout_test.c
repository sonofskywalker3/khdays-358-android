/* khdays-present-layout-test: the two-screen picture's places (host/render/two_screens.c): stacked as the DS has
 * them, the screens as wide as they were drawn in the middle of the picture's columns; side by side, the upper on
 * the left and the lower on the right, each the DS's 256 columns. */
#include "../host/render/two_screens.h"
#include "../host/render/auto_quality.h"

#include <stdio.h>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                                      \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static int same(KhdaysScreenPlace p, float x, float y, float w, float h)
{
    return p.x == x && p.y == y && p.w == w && p.h == h;
}

int main(void)
{
    /* stacked at the DS's width: today's picture, 256 x 384 */
    KhdaysTwoScreensLayout l = khdays_two_screens_layout(0, 256, 256.0f, 256.0f);
    CHECK(l.width == 256 && l.height == 384);
    CHECK(same(l.upper, 0, 0, 256, 192) && same(l.lower, 0, 192, 256, 192));
    /* stacked at 16:9: the field's screen drawn 342 wide, the other 256 in the middle */
    l = khdays_two_screens_layout(0, 342, 342.0f, 256.0f);
    CHECK(l.width == 342 && l.height == 384);
    CHECK(same(l.upper, 0, 0, 342, 192) && same(l.lower, 43, 192, 256, 192));
    /* side by side: 512 x 192, whatever the columns asked */
    l = khdays_two_screens_layout(1, 342, 342.0f, 256.0f);
    CHECK(l.width == 512 && l.height == 192);
    CHECK(same(l.upper, 0, 0, 256, 192) && same(l.lower, 256, 0, 256, 192));
    /* the 3D on both screens: the other screen's copy, of the package just before or a few VBlanks old */
    CHECK(khdays_two_screens_kept_shows(101, 100, 100, 1, 0)); /* each VBlank drawn */
    CHECK(khdays_two_screens_kept_shows(103, 100, 102, 1, 0)); /* a package not drawn between: the copy stays */
    CHECK(!khdays_two_screens_kept_shows(100 + KHDAYS_KEPT_VBLANKS_BOTH + 1, 100, 104, 1, 0)); /* too old */
    CHECK(khdays_two_screens_kept_shows(105, 100, 101, 1, 1)); /* drawn two in four, one missed, capturing */
    CHECK(khdays_two_screens_kept_shows(100 + KHDAYS_KEPT_VBLANKS_ARMED, 100, 104, 1, 1));      /* still capturing */
    CHECK(!khdays_two_screens_kept_shows(100 + KHDAYS_KEPT_VBLANKS_ARMED + 1, 100, 104, 1, 1)); /* too old */
    CHECK(
        khdays_two_screens_kept_shows(100 + KHDAYS_KEPT_VBLANKS_BOTH, 100, 104, 1, 0)); /* a change of scene's still */
    {
        KhdaysArmedSeen seen = {0};
        CHECK(!khdays_two_screens_armed_seen(&seen, 50, 0)); /* none yet */
        CHECK(khdays_two_screens_armed_seen(&seen, 0, 1));   /* VBlank 0 counts */
        CHECK(khdays_two_screens_armed_seen(&seen, 100, 1));
        CHECK(khdays_two_screens_armed_seen(&seen, 100 + KHDAYS_KEPT_VBLANKS, 0));  /* a frame over two VBlanks */
        CHECK(!khdays_two_screens_armed_seen(&seen, 101 + KHDAYS_KEPT_VBLANKS, 0)); /* stopped capturing */
        /* a VBlank between two views waits; an armed one, one without the 3D on both, and one after the game
         * stopped capturing are drawn */
        CHECK(khdays_two_screens_waits(&seen, 101, 1, 0));
        CHECK(!khdays_two_screens_waits(&seen, 101, 1, 1));
        CHECK(!khdays_two_screens_waits(&seen, 101, 0, 0));
        CHECK(khdays_two_screens_waits(&seen, 100 + KHDAYS_WAIT_VBLANKS, 1, 0));
        CHECK(!khdays_two_screens_waits(&seen, 101 + KHDAYS_WAIT_VBLANKS, 1, 0));
        CHECK(!khdays_two_screens_waits(&seen, 99, 1, 0));
        CHECK(!khdays_two_screens_armed_seen(&seen, 90, 0));  /* went back: forgotten */
        CHECK(!khdays_two_screens_armed_seen(&seen, 102, 0)); /* ... for good */
    }
    CHECK(khdays_two_screens_kept_shows(110, 100, 100, 0, 0));  /* one change of screens: the package before */
    CHECK(!khdays_two_screens_kept_shows(103, 100, 102, 0, 0)); /* ... and no older one */
    CHECK(!khdays_two_screens_kept_shows(100, 100, 100, 1, 0)); /* its own picture */
    /* each screen's 2D scale: as chosen, auto's for 0, never below the 3D's for the 3D's engine */
    CHECK(khdays_two_screens_scale2d(2, 5, 1) == 2);
    CHECK(khdays_two_screens_scale2d(0, 4, 1) == 4);
    CHECK(khdays_two_screens_scale2d(2, 5, 3) == 3);
    CHECK(khdays_two_screens_scale2d(0, 0, 0) == 1);

    /* Auto's search, at the Thor's measures (one screen: 5/2 70, 4/2 100, 4/3 95, 4/4 78): the upper down from its
     * most until a pair holds 80, then the lower up while it holds */
    {
        const KhdaysAutoSetup both = {1, 1, 0, 0, 5, 4, KHDAYS_AUTO_TARGET};
        KhdaysAutoSearch s;
        khdays_auto_search_start(&s, &both);
        CHECK(s.upper == 5 && s.lower == 2);
        CHECK(khdays_auto_search_measured(&s, 70) && s.upper == 4 && s.lower == 2);
        CHECK(khdays_auto_search_measured(&s, 100) && s.upper == 4 && s.lower == 3);
        CHECK(khdays_auto_search_measured(&s, 95) && s.upper == 4 && s.lower == 4);
        CHECK(!khdays_auto_search_measured(&s, 78)); /* 4/4 fails: 4/3 kept */
        CHECK(s.best_upper == 4 && s.best_lower == 3 && s.best_fps == 95);
        /* none holds: the least tried */
        khdays_auto_search_start(&s, &both);
        for (int i = 0; i < 3; ++i) {
            CHECK(khdays_auto_search_measured(&s, 30));
        }
        CHECK(!khdays_auto_search_measured(&s, 40) && s.best_upper == 2 && s.best_lower == 2 && s.best_fps == 40);
        /* the lower raised to its own display's most, no further */
        khdays_auto_search_start(&s, &both);
        CHECK(khdays_auto_search_measured(&s, 90) && s.lower == 3);
        CHECK(khdays_auto_search_measured(&s, 90) && s.lower == 4);
        CHECK(!khdays_auto_search_measured(&s, 90) && s.best_upper == 5 && s.best_lower == 4);
        /* out of time before anything held: the last pair tried */
        khdays_auto_search_start(&s, &both);
        CHECK(khdays_auto_search_measured(&s, 50));
        khdays_auto_search_finish(&s);
        CHECK(s.best_upper == 4 && s.best_lower == 2 && s.best_fps == 50);
    }
    {
        /* the upper chosen by the player (3x): only the lower searched */
        const KhdaysAutoSetup lower_only = {0, 1, 3, 0, 5, 4, KHDAYS_AUTO_TARGET};
        KhdaysAutoSearch s;
        khdays_auto_search_start(&s, &lower_only);
        CHECK(s.upper == 3 && s.lower == 2);
        CHECK(khdays_auto_search_measured(&s, 120) && s.upper == 3 && s.lower == 3);
        CHECK(!khdays_auto_search_measured(&s, 70) && s.best_upper == 3 && s.best_lower == 2);
        /* the lower chosen (2x): only the upper searched, the lower never raised */
        const KhdaysAutoSetup upper_only = {1, 0, 0, 2, 5, 4, KHDAYS_AUTO_TARGET};
        khdays_auto_search_start(&s, &upper_only);
        CHECK(khdays_auto_search_measured(&s, 60) && s.upper == 4 && s.lower == 2);
        CHECK(!khdays_auto_search_measured(&s, 100) && s.best_upper == 4 && s.best_lower == 2);
        /* a display that fits under 2x: still 2x */
        const KhdaysAutoSetup small = {1, 1, 0, 0, 1, 1, KHDAYS_AUTO_TARGET};
        khdays_auto_search_start(&s, &small);
        CHECK(s.upper == 2 && s.lower == 2);
        CHECK(!khdays_auto_search_measured(&s, 100) && s.best_upper == 2 && s.best_lower == 2);
    }
    {
        /* the watch: five seconds in a row under 60 owe a step; a good second starts the count again */
        KhdaysAutoWatch w = {0, 0};
        for (int i = 0; i < KHDAYS_AUTO_LOW_SECONDS - 1; ++i) {
            khdays_auto_watch_second(&w, 50, KHDAYS_AUTO_FLOOR);
        }
        khdays_auto_watch_second(&w, 61, KHDAYS_AUTO_FLOOR);
        CHECK(!w.owed);
        for (int i = 0; i < KHDAYS_AUTO_LOW_SECONDS; ++i) {
            khdays_auto_watch_second(&w, 50, KHDAYS_AUTO_FLOOR);
        }
        CHECK(w.owed);
        /* a 50 Hz display's floor: 45 a second is play as it should be there */
        KhdaysAutoWatch slow = {0, 0};
        for (int i = 0; i < 2 * KHDAYS_AUTO_LOW_SECONDS; ++i) {
            khdays_auto_watch_second(&slow, 45, 44);
        }
        CHECK(!slow.owed);
        /* the step: the lower first, then the upper, never under 2; a screen the player chose stays */
        int upper = 4, lower = 3;
        CHECK(khdays_auto_step_down(1, 1, &upper, &lower) && upper == 4 && lower == 2);
        CHECK(khdays_auto_step_down(1, 1, &upper, &lower) && upper == 3 && lower == 2);
        upper = 2;
        CHECK(!khdays_auto_step_down(1, 1, &upper, &lower) && upper == 2 && lower == 2);
        upper = 4;
        lower = 3;
        CHECK(khdays_auto_step_down(1, 0, &upper, &lower) && upper == 3 && lower == 3);
    }
    if (failures == 0) {
        printf("present layout: ok\n");
    }
    return failures != 0;
}
