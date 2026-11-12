/* khdays-present-layout-test: the two-screen picture's places (host/render/two_screens.c): stacked as the DS has
 * them, the screens as wide as they were drawn in the middle of the picture's columns; side by side, the upper on
 * the left and the lower on the right, each the DS's 256 columns. */
#include "../host/render/two_screens.h"

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
    CHECK(khdays_two_screens_kept_shows(101, 100, 100, 1));  /* each VBlank drawn */
    CHECK(khdays_two_screens_kept_shows(103, 100, 102, 1));  /* a package not drawn between: the copy stays */
    CHECK(!khdays_two_screens_kept_shows(100 + KHDAYS_KEPT_VBLANKS + 1, 100, 104, 1)); /* too old */
    CHECK(khdays_two_screens_kept_shows(110, 100, 100, 0));  /* one change of screens: the package before */
    CHECK(!khdays_two_screens_kept_shows(103, 100, 102, 0)); /* ... and no older one */
    CHECK(!khdays_two_screens_kept_shows(100, 100, 100, 1)); /* its own picture */
    if (failures == 0) {
        printf("present layout: ok\n");
    }
    return failures != 0;
}
