/* khdays-lower-layout-test: the lower screen laid out as tall as the Thor's lower display
 * (host/screens/lower_layout.h), on synthetic screens: each told by its pixels, the DS's shape for one
 * it does not know, the pieces where the designs put them (docs/LOWER_DISPLAY.md), the characters'
 * zoom measured from their grid, and a touch going back to the DS pixel it shows. */
#include "../host/screens/lower_layout.h"
#include "../host/screens/lower_screens.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                                      \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

#define DS_W 256
#define DS_H 192
#define SCALE 4              /* the picture: the DS's screen at 4x */
#define OUT_W (DS_W * SCALE) /* the display: as wide, 31 DS rows taller (the Thor's 1240 x 1080 in shape) */
#define OUT_H 892

static uint8_t picture[DS_H * SCALE][DS_W * SCALE][4];
static uint8_t out[OUT_H][OUT_W][4];
static uint32_t ds[DS_W * (DS_H + KHDAYS_LOWER_OVERSCAN)];

/* A DS pixel of the picture (all its SCALE x SCALE). */
static void put(int x, int y, uint32_t rgb)
{
    for (int dy = 0; dy < SCALE; ++dy) {
        for (int dx = 0; dx < SCALE; ++dx) {
            uint8_t *p = picture[y * SCALE + dy][x * SCALE + dx];
            p[0] = (uint8_t)(rgb >> 16);
            p[1] = (uint8_t)(rgb >> 8);
            p[2] = (uint8_t)rgb;
            p[3] = 255;
        }
    }
}
static void rect(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            put(x, y, rgb);
        }
    }
}
/* Each DS pixel its own colour, so where one lands tells where it came from. */
static void numbered(void)
{
    for (int y = 0; y < DS_H; ++y) {
        for (int x = 0; x < DS_W; ++x) {
            put(x, y, 0x010000u * (uint32_t)(y & 127) | 0x000100u * (uint32_t)(x & 127) | 0x7fu);
        }
    }
}

static const KhdaysLowerPicture pic = {&picture[0][0][0], DS_W *SCALE, DS_H *SCALE, DS_W *SCALE * 4, NULL, 0, 0, 0};

/* Lays the picture out; the screen told. */
static int lay(KhdaysLowerLayout *l)
{
    int rows, screen;
    khdays_lower_sample(&pic, ds, &rows);
    screen = khdays_lower_screen(ds, rows);
    CHECK(khdays_lower_layout(l, &pic, ds, rows, screen, OUT_W, OUT_H));
    khdays_lower_draw(l, &pic, &out[0][0][0], OUT_W * 4);
    return screen;
}

/* The DS pixel the display's middle of DS dot (ox, oy) shows: through the touch's way back. */
static int shows(const KhdaysLowerLayout *l, float ox, float oy, int *x, int *y)
{
    return khdays_lower_touch(l, (int)(ox * SCALE), (int)(oy * SCALE), x, y);
}

static void test_unknown(void)
{
    KhdaysLowerLayout l = {0};
    int x, y;
    numbered();
    CHECK(lay(&l) == KHDAYS_LOWER_NONE);
    /* centred: about 15.5 DS rows of black above and below */
    CHECK(out[10][100][0] == 0 && out[10][100][3] == 255);
    CHECK(!shows(&l, 100, 5, &x, &y));
    CHECK(shows(&l, 100.5f, 100.5f, &x, &y) && x == 100 && (y == 84 || y == 85));
    khdays_lower_free(&l);
}

/* The picture faded `white` / 255 of the way to white. */
static void whiten(int white)
{
    for (int yy = 0; yy < DS_H; ++yy) {
        for (int xx = 0; xx < DS_W; ++xx) {
            uint8_t *p = picture[yy * SCALE][xx * SCALE];
            uint32_t c = 0;
            for (int k = 0; k < 3; ++k) {
                c = c << 8 | (uint32_t)(p[k] + (255 - p[k]) * white / 255);
            }
            put(xx, yy, c);
        }
    }
}

/* The picture dimmed to `keep` / 255, as under a dialog. */
static void dim(int keep)
{
    for (int yy = 0; yy < DS_H; ++yy) {
        for (int xx = 0; xx < DS_W; ++xx) {
            uint8_t *p = picture[yy * SCALE][xx * SCALE];
            put(xx, yy,
                (uint32_t)(p[0] * keep / 255) << 16 | (uint32_t)(p[1] * keep / 255) << 8 |
                    (uint32_t)(p[2] * keep / 255));
        }
    }
}

static void field(void)
{
    numbered();
    rect(0, 0, DS_W, 23, 0x000000u);
    rect(0, 23, DS_W, 24, 0xffffffu);  /* the line under the objective */
    rect(24, 28, 232, 148, 0xffffffu); /* the panel */
    rect(32, 64, 97, 140, 0x000000u);  /* the target's box */
}

static void test_field(void)
{
    KhdaysLowerLayout l = {0};
    int x, y;
    field();
    CHECK(lay(&l) == KHDAYS_LOWER_FIELD);
    CHECK(shows(&l, 50.5f, 10.5f, &x, &y) && x == 50 && y == 10);                 /* the header stays */
    CHECK(shows(&l, 50.5f, 222.5f, &x, &y) && x == 50 && (y == 191 || y == 190)); /* the bar at the bottom */
    CHECK(shows(&l, 60.5f, 140.5f, &x, &y) && y >= 67 && y < 137);                /* inside the grown target box */
    CHECK(shows(&l, 10.5f, 170.5f, &x, &y) && y < 148);                           /* the pattern beside it, again */
    khdays_lower_free(&l);
    /* the pause dims it: still the field */
    field();
    for (int yy = 0; yy < DS_H; ++yy) {
        for (int xx = 0; xx < DS_W; ++xx) {
            uint8_t *p = picture[yy * SCALE][xx * SCALE];
            put(xx, yy,
                (uint32_t)(p[0] * 134 / 255) << 16 | (uint32_t)(p[1] * 134 / 255) << 8 | (uint32_t)(p[2] * 134 / 255));
        }
    }
    CHECK(lay(&l) == KHDAYS_LOWER_FIELD);
    khdays_lower_free(&l);
    /* a mission starting: the panel there, the header still sliding in */
    field();
    rect(0, 0, DS_W, 28, 0x212121u);
    rect(24, 30, 232, 31, 0x938c93u); /* the panel's lilac top edge */
    CHECK(lay(&l) == KHDAYS_LOWER_FIELD);
    dim(97); /* fading in from black */
    CHECK(lay(&l) == KHDAYS_LOWER_FIELD);
    khdays_lower_free(&l);
}

static void test_load(void)
{
    KhdaysLowerLayout l = {0};
    int x, y;
    numbered();
    rect(0, 14, DS_W, 15, 0x142167u); /* the header's blue line */
    rect(0, 15, DS_W, 16, 0x000000u);
    rect(0, 144, DS_W, 146, 0x424242u); /* the divider over the help line */
    CHECK(lay(&l) == KHDAYS_LOWER_LOAD);
    CHECK(shows(&l, 30.5f, 5.5f, &x, &y) && x == 30 && y == 5);                       /* the header stays */
    CHECK(shows(&l, 30.5f, 200.5f, &x, &y) && x == 30 && y == 169);                   /* the help line at the bottom */
    CHECK(shows(&l, 128.0f, 95.0f, &x, &y) && abs(x - 130) <= 1 && abs(y - 79) <= 1); /* the list's middle */
    CHECK(shows(&l, 30.0f, 95.0f, &x, &y) && x > 30);                                 /* the list enlarged about it */
    {
        /* through a fade the divider is found as before: the same layout */
        const int key = l.key;
        for (int keep = 240; keep >= 60; keep -= 30) {
            numbered();
            rect(0, 14, DS_W, 15, 0x142167u);
            rect(0, 15, DS_W, 16, 0x000000u);
            rect(0, 20, DS_W, 21, 0x212121u); /* a plain row of the list's backdrop */
            rect(0, 144, DS_W, 146, 0x424242u);
            dim(keep);
            CHECK(lay(&l) == KHDAYS_LOWER_LOAD && l.key == key);
        }
        for (int white = 30; white <= 210; white += 30) {
            /* and through a fade to white */
            numbered();
            rect(0, 14, DS_W, 15, 0x142167u);
            rect(0, 15, DS_W, 16, 0x000000u);
            rect(0, 20, DS_W, 21, 0x000000u);
            rect(0, 144, DS_W, 146, 0x424242u);
            whiten(white);
            CHECK(lay(&l) == KHDAYS_LOWER_LOAD && l.key == key);
        }
    }
    khdays_lower_free(&l);
}

/* A display only a little taller than the DS's: Load enlarged no more than it has rows for. */
static void test_load_short(void)
{
    KhdaysLowerPlace place;
    int key, rows;
    numbered();
    rect(0, 14, DS_W, 15, 0x142167u);
    rect(0, 15, DS_W, 16, 0x000000u);
    rect(0, 144, DS_W, 146, 0x424242u);
    khdays_lower_sample(&pic, ds, &rows);
    CHECK(khdays_lower_screens_place(KHDAYS_LOWER_LOAD, ds, DS_H, (float)DS_H + 4.0f, &place, &key));
    CHECK(place.zoom <= ((float)(place.foot - place.top) + 4.0f) / (float)(place.foot - place.top) + 0.0001f);
}

static void characters(int grid_x0, int grid_x1)
{
    rect(0, 0, DS_W, DS_H, 0x242424u);
    rect(40, 20, 140, 21, 0x000000u);           /* the black band under the title */
    rect(grid_x0, 32, grid_x1, 160, 0xd0d0d0u); /* the portraits */
    rect(0, 176, DS_W, 177, 0xffffffu);         /* the line over the help */
    rect(0, 177, DS_W, DS_H, 0x000000u);
}

static void test_characters(void)
{
    KhdaysLowerLayout l = {0};
    int x, y;
    /* the base roster: room for the most zoom */
    characters(44, 212);
    CHECK(lay(&l) == KHDAYS_LOWER_CHARACTERS);
    CHECK(shows(&l, 128.0f, 30.0f, &x, &y) && y < 32);               /* above the grid */
    CHECK(fabsf((float)l.key - (175.0f * 1000.0f + 120.0f)) < 1.5f); /* zoom 1.2 */
    /* every character: the zoom keeps 4 DS columns free each side */
    characters(8, 248);
    CHECK(lay(&l) == KHDAYS_LOWER_CHARACTERS);
    CHECK(l.key % 1000 < 104 && l.key % 1000 >= 100);
    {
        /* through a fade to black: still the characters, laid out alike */
        const int key = l.key;
        for (int keep = 230; keep >= 80; keep -= 30) {
            characters(8, 248);
            dim(keep);
            CHECK(lay(&l) == KHDAYS_LOWER_CHARACTERS && l.key == key);
        }
    }
    CHECK(shows(&l, 1.5f, 100.0f, &x, &y) && x < 8); /* the backdrop beside the grid */
    CHECK(shows(&l, 254.5f, 100.0f, &x, &y) && x >= 248);
    khdays_lower_free(&l);
    /* the multiplayer groups' page: the same line and dark top, no title band: not the characters */
    characters(60, 200);
    rect(40, 20, 140, 21, 0x212121u);
    CHECK(lay(&l) != KHDAYS_LOWER_CHARACTERS);
    khdays_lower_free(&l);
}

static void test_status(void)
{
    KhdaysLowerLayout l = {0};
    int x, y, lv = 0, stat = 0;
    numbered();
    rect(0, 0, 72, DS_H, 0x6d141cu);    /* the red panel */
    rect(72, 0, DS_W, DS_H, 0x242424u); /* the stats */
    put(100, 120, 0x000000u);
    CHECK(lay(&l) == KHDAYS_LOWER_STATUS);
    /* each panel's rows spread: the last rows of both at the bottom, the first at the top */
    CHECK(shows(&l, 30.5f, 0.5f, &x, &y) && y == 0);
    CHECK(shows(&l, 30.5f, 222.5f, &x, &y) && y == 191);
    CHECK(shows(&l, 150.5f, 222.5f, &x, &y) && y == 191);
    for (int oy = 0; oy < 223; ++oy) {
        lv += shows(&l, 30.5f, (float)oy + 0.5f, &x, &y) && y == 82;    /* LV's row, repeated */
        stat += shows(&l, 150.5f, (float)oy + 0.5f, &x, &y) && y == 28; /* a stat's */
    }
    CHECK(lv == 7 && stat == 5);
    khdays_lower_free(&l);
}

static void summary(int icons)
{
    numbered();
    rect(0, 0, DS_W, 56, 0x212121u);
    rect(8, 8, 248, 40, 0x62191bu);  /* the card */
    rect(8, 40, 128, 56, 0xb63337u); /* the Summary tab, chosen */
    rect(128, 40, 248, 56, 0x424242u);
    if (icons) {
        rect(2, 2, 14, 12, 0x429230u);    /* L */
        rect(241, 2, 253, 12, 0x429230u); /* R */
    }
    rect(8, 56, 248, 168, 0xffffffu); /* the page */
    rect(0, 168, DS_W, 176, 0x242424u);
    rect(0, 176, DS_W, 177, 0xffffffu);
    rect(0, 177, DS_W, DS_H, 0x000000u);
}

static void test_summary(void)
{
    KhdaysLowerLayout l = {0};
    int x, y;
    summary(1);
    CHECK(lay(&l) == KHDAYS_LOWER_SUMMARY);
    CHECK(shows(&l, 4.5f, 6.5f, &x, &y) && x == 4 && y == 6);          /* L at its own place and size */
    CHECK(shows(&l, 0.5f, 100.0f, &x, &y) && x == 8);                  /* the page from the edge */
    CHECK(shows(&l, 30.5f, 222.5f, &x, &y) && (y == 191 || y == 190)); /* Embark / Back at the bottom */
    {
        /* the added line is blank: no row of the last line's text (146-158) shows twice */
        int seen[DS_H] = {0};
        for (int oy = 0; oy < 223; ++oy) {
            if (shows(&l, 100.5f, (float)oy + 0.5f, &x, &y)) {
                ++seen[y];
            }
        }
        for (int k = 146; k < 159; ++k) {
            CHECK(seen[k] <= 2); /* the 1.067 zoom shows a row at most twice */
        }
        CHECK(seen[144] > 10);
    }
    /* the page's text scrolling by (dark and red letters across the middle): still the summary */
    summary(1);
    rect(24, 96, 232, 124, 0x3a3a3au);
    rect(24, 100, 232, 104, 0xea3729u);
    CHECK(lay(&l) == KHDAYS_LOWER_SUMMARY);
    khdays_lower_free(&l);
    /* the Embark dialog dims it, icons and all: still the summary */
    summary(1);
    dim(128);
    CHECK(lay(&l) == KHDAYS_LOWER_SUMMARY);
    khdays_lower_free(&l);
    /* the story's: no L and R icons, the corners enlarged with the rest */
    summary(0);
    CHECK(lay(&l) == KHDAYS_LOWER_SUMMARY);
    CHECK(shows(&l, 4.5f, 6.5f, &x, &y) && x == 8 + (int)(4.5f * 240.0f / 256.0f));
    summary(0);
    dim(128);
    CHECK(lay(&l) == KHDAYS_LOWER_SUMMARY);
    /* another tab chosen: the Summary tab grey */
    summary(1);
    rect(8, 40, 128, 56, 0x424242u);
    CHECK(lay(&l) == KHDAYS_LOWER_SUMMARY);
    khdays_lower_free(&l);
}

/* A list of missions as the Thor draws it: its greys 107 and 148 (the tutorials' 109 and 150). */
static void test_missions(void)
{
    KhdaysLowerLayout l = {0};
    numbered();
    rect(0, 14, DS_W, 15, 0x212121u);
    put(100, 14, 0x4a4a4au);          /* no header bar across */
    rect(10, 20, 11, 180, 0x6b6b6bu); /* the frame's edge */
    rect(9, 20, 10, 180, 0x4a4a4au);
    rect(232, 20, 248, 176, 0x6b6b6bu);  /* the scroll track */
    rect(236, 172, 244, 175, 0x949494u); /* its rounded end */
    put(232, 175, 0x6b6b6bu);
    CHECK(lay(&l) == KHDAYS_LOWER_MISSIONS);
    CHECK(l.screen == KHDAYS_LOWER_NONE && l.want_rows > 0); /* the DS's shape until its extra rows come */
    rect(0, 96, 16, 104, 0xffffffu); /* the pointing hand beside the chosen entry, over the frame's edge */
    CHECK(lay(&l) == KHDAYS_LOWER_MISSIONS);
    khdays_lower_free(&l);
}

/* The list grown with its extra rows: the frame's sides and the scroll track below the DS's rows take a
 * plain row of theirs, not the hand's beside the chosen entry nor the track's thumb. */
static uint8_t overscan[32 * SCALE][DS_W * SCALE][4];

static void test_missions_grown(void)
{
    KhdaysLowerLayout l = {0};
    KhdaysLowerPicture grown = pic;
    int rows, x, y;
    numbered();
    rect(0, 14, DS_W, 15, 0x212121u);
    put(100, 14, 0x4a4a4au);
    rect(0, 20, 16, 180, 0x303030u);  /* the frame's left side, plain */
    rect(10, 20, 11, 180, 0x6b6b6bu); /* its edge */
    rect(9, 20, 10, 180, 0x4a4a4au);
    rect(222, 20, 248, 176, 0x6b6b6bu); /* the scroll track */
    rect(236, 172, 244, 175, 0x949494u);
    put(232, 175, 0x6b6b6bu);
    rect(0, 96, 16, 104, 0xffffffu);    /* the hand */
    rect(224, 90, 242, 112, 0xc0c0c0u); /* the thumb */
    grown.overscan = &overscan[0][0][0];
    grown.overscan_rows = 32 * SCALE;
    grown.overscan_pitch = DS_W * SCALE * 4;
    grown.overscan_row0 = 176;
    khdays_lower_sample(&grown, ds, &rows);
    CHECK(khdays_lower_screen(ds, rows) == KHDAYS_LOWER_MISSIONS);
    CHECK(khdays_lower_layout(&l, &grown, ds, rows, KHDAYS_LOWER_MISSIONS, OUT_W, OUT_H));
    CHECK(l.screen == KHDAYS_LOWER_MISSIONS);
    CHECK(shows(&l, 2.5f, 200.5f, &x, &y) && (y < 96 || y >= 104));   /* the side under the frame */
    CHECK(shows(&l, 246.5f, 180.5f, &x, &y) && (y < 90 || y >= 112)); /* the track's added rows */
    khdays_lower_free(&l);
}

/* How many of the display's rows show DS row `row` at column x. */
static int shown(const KhdaysLowerLayout *l, float x, int row)
{
    int n = 0, sx, sy;
    for (int oy = 0; oy < 223; ++oy) {
        n += shows(l, x, (float)oy + 0.5f, &sx, &sy) && sy == row;
    }
    return n;
}

static void test_tutorials(void)
{
    KhdaysLowerLayout l = {0};
    numbered();
    rect(0, 14, DS_W, 15, 0x641c1fu); /* the header's red line */
    rect(0, 15, DS_W, 16, 0x000000u);
    rect(0, 16, DS_W, 17, 0x1c1c1cu); /* the frame's stripes */
    rect(0, 26, DS_W, 27, 0x969696u); /* its top edge */
    CHECK(lay(&l) == KHDAYS_LOWER_TUTORIALS);
    /* the gaps between entries grow alike, the entries keep their rows */
    CHECK(shown(&l, 100.5f, 48) >= 4 && shown(&l, 100.5f, 48) <= 5);
    CHECK(shown(&l, 100.5f, 160) >= 4 && shown(&l, 100.5f, 160) <= 5);
    CHECK(shown(&l, 100.5f, 40) == 1);
    CHECK(shown(&l, 100.5f, 191) == 1);
    khdays_lower_free(&l);
}

static void test_diary(void)
{
    KhdaysLowerLayout l = {0};
    numbered();
    rect(0, 0, DS_W, 2, 0x3e6c2eu); /* the backdrop's stripes */
    rect(0, 2, DS_W, 4, 0x487b37u);
    rect(0, 12, DS_W, 13, 0x789d40u); /* its plain green */
    rect(0, 188, DS_W, 190, 0x3e6c2eu);
    CHECK(lay(&l) == KHDAYS_LOWER_DIARY);
    CHECK(shown(&l, 100.5f, 32) >= 3 && shown(&l, 100.5f, 160) >= 3);
    rect(116, 8, 124, 14, 0x9f2c31u); /* scrolled: the up arrow over the plain green */
    CHECK(lay(&l) == KHDAYS_LOWER_DIARY);
    khdays_lower_free(&l);
}

static void test_panels(void)
{
    KhdaysLowerLayout l = {0};
    numbered();
    rect(0, 0, 95, 157, 0x525259u);    /* the board's frame */
    rect(95, 24, 232, 157, 0x243148u); /* the stock */
    rect(8, 160, 248, 161, 0x212121u);
    rect(8, 161, 248, 162, 0xffffffu); /* the help box */
    rect(8, 190, 248, 191, 0xffffffu);
    CHECK(lay(&l) == KHDAYS_LOWER_PANELS);
    /* the board grows above and below its grid; the stock's entries apart */
    CHECK(shown(&l, 50.5f, 22) >= 15 && shown(&l, 50.5f, 152) >= 15);
    CHECK(shown(&l, 150.5f, 24) >= 4 && shown(&l, 150.5f, 136) >= 4);
    CHECK(shown(&l, 150.5f, 191) == 1);
    khdays_lower_free(&l);
}

/* A fade to white: all of the display that white, no bars; black keeps the DS's shape. */
static void test_plain(void)
{
    KhdaysLowerLayout l = {0};
    int x, y;
    rect(0, 0, DS_W, DS_H, 0xefefefu);
    put(100, 100, 0xeeeeeeu);
    CHECK(lay(&l) == KHDAYS_LOWER_PLAIN);
    CHECK(out[10][100][0] == 0xef && shows(&l, 100.5f, 2.5f, &x, &y));
    rect(0, 0, DS_W, DS_H, 0x000000u);
    CHECK(lay(&l) == KHDAYS_LOWER_NONE);
    khdays_lower_free(&l);
}

/* The title: the DS's screen at the top, the copyright line's grey band longer under it. */
static void test_title(void)
{
    KhdaysLowerLayout l = {0};
    int x, y;
    numbered();
    rect(0, 176, DS_W, 180, 0xffffffu);  /* the picture's white above the band */
    rect(0, 180, DS_W, DS_H, 0x868686u); /* the copyright line's band */
    CHECK(lay(&l) == KHDAYS_LOWER_TITLE);
    CHECK(shows(&l, 100.5f, 100.5f, &x, &y) && x == 100 && y == 100); /* the screen as it is, at the top */
    CHECK(shows(&l, 250.5f, 185.5f, &x, &y) && x == 250 && y == 185); /* the line's text where it was */
    CHECK(shows(&l, 200.5f, 215.5f, &x, &y) && x == 2 && y == 186);   /* the band's grey below */
    dim(60);                                                          /* fading in */
    CHECK(lay(&l) == KHDAYS_LOWER_TITLE);
    khdays_lower_free(&l);
}

/* A day's missions: the slots spaced apart below the day's card. */
static void test_day(void)
{
    KhdaysLowerLayout l = {0};
    numbered();
    rect(10, 9, 246, 37, 0x62191bu); /* the day's card */
    rect(8, 37, 248, 40, 0x000000u);
    rect(16, 40, 244, 41, 0x080808u);   /* the frame's top line */
    rect(224, 41, 240, 176, 0xa5a5acu); /* the scroll track */
    rect(240, 41, 248, 176, 0x212121u); /* the frame's edge */
    CHECK(lay(&l) == KHDAYS_LOWER_DAY);
    CHECK(shown(&l, 100.5f, 80) >= 5 && shown(&l, 100.5f, 144) >= 5); /* the rows between the slots */
    CHECK(shown(&l, 100.5f, 20) == 1);                                /* the card at its own size */
    dim(40);                                                          /* closing */
    CHECK(lay(&l) == KHDAYS_LOWER_DAY);
    khdays_lower_free(&l);
}

/* A logo on white: the bars white too, the logo at its size. */
static void test_backdrop(void)
{
    KhdaysLowerLayout l = {0};
    int x, y;
    rect(0, 0, DS_W, DS_H, 0xffffffu);
    rect(60, 90, 200, 100, 0x404040u); /* the logo */
    CHECK(lay(&l) == KHDAYS_LOWER_BACKDROP);
    CHECK(out[4][500][0] == 0xff && out[OUT_H - 4][500][0] == 0xff);
    CHECK(shows(&l, 100.5f, 95.0f + 15.5f, &x, &y) && y >= 90 && y < 100);
    khdays_lower_free(&l);
}

/* Screens told while the game fades them: a mission list, the Load list lightened toward white. */
static void test_fades(void)
{
    KhdaysLowerLayout l = {0};
    numbered();
    rect(0, 14, DS_W, 15, 0x212121u);
    put(100, 14, 0x4a4a4au);
    rect(10, 20, 11, 180, 0x6b6b6bu);
    rect(9, 20, 10, 180, 0x4a4a4au);
    rect(232, 20, 248, 176, 0x6b6b6bu);
    rect(236, 172, 244, 175, 0x949494u);
    put(232, 175, 0x6b6b6bu);
    dim(209); /* the Thor's 87 and 121 */
    CHECK(lay(&l) == KHDAYS_LOWER_MISSIONS);
    numbered();
    rect(0, 14, DS_W, 15, 0x3c4a86u); /* the header's blue line, lightened */
    rect(0, 15, DS_W, 16, 0x2f2f2fu); /* the black under it, lightened */
    CHECK(lay(&l) == KHDAYS_LOWER_LOAD);
    khdays_lower_free(&l);
}

/* A display wider than 4:3 shows the DS's shape at its height, black beside it: none of the DS's rows
 * left out. */
#define WIDE_W ((DS_W + 64) * SCALE)
static uint8_t wide_out[DS_H * SCALE][WIDE_W][4];

static void test_wider_display(void)
{
    KhdaysLowerLayout l = {0};
    int x, y, rows;
    numbered();
    khdays_lower_sample(&pic, ds, &rows);
    CHECK(khdays_lower_layout(&l, &pic, ds, rows, khdays_lower_screen(ds, rows), WIDE_W, DS_H * SCALE));
    CHECK(l.screen == KHDAYS_LOWER_NONE);
    khdays_lower_draw(&l, &pic, &wide_out[0][0][0], WIDE_W * 4);
    CHECK(wide_out[100][4][0] == 0 && wide_out[100][4][3] == 255);                /* black beside it */
    CHECK(khdays_lower_touch(&l, 32 * SCALE + 2, 2, &x, &y) && x == 0 && y == 0); /* the first row, whole */
    CHECK(khdays_lower_touch(&l, 32 * SCALE + 2, DS_H * SCALE - 2, &x, &y) && x == 0 && y == DS_H - 1);
    CHECK(!khdays_lower_touch(&l, 4, 100, &x, &y));
    khdays_lower_free(&l);
}

/* The shop: above the help box enlarged, the lists' L and R icons in the corners at their own size, the
 * help box at its own size at the bottom; dimmed as it closes, still the shop. */
static void test_shop(void)
{
    KhdaysLowerLayout l = {0};
    int x, y;
    numbered();
    rect(0, 160, DS_W, 161, 0x9a5720u); /* the help box's border */
    rect(0, 161, DS_W, 191, 0x4d2b0au); /* its inside */
    rect(0, 191, DS_W, DS_H, 0x9a5720u);
    rect(18, 1, 31, 14, 0x4faa40u);   /* the L icon */
    rect(225, 1, 238, 14, 0x4faa40u); /* the R icon */
    CHECK(lay(&l) == KHDAYS_LOWER_SHOP);
    CHECK(shows(&l, 4.5f, 6.5f, &x, &y) && x == 20 && y == 6);       /* L in the corner */
    CHECK(shows(&l, 250.5f, 6.5f, &x, &y) && x == 234 && y == 6);    /* R in the other */
    CHECK(shows(&l, 30.5f, 215.5f, &x, &y) && x == 30 && y == 184);  /* the help box at its size */
    CHECK(shows(&l, 128.5f, 100.5f, &x, &y) && x == 128 && y < 100); /* the middle enlarged */
    CHECK(shows(&l, 30.5f, 100.5f, &x, &y) && x > 30);
    dim(60);
    CHECK(lay(&l) == KHDAYS_LOWER_SHOP);
    khdays_lower_free(&l);
}

/* A mission's review: its ruled rows apart, the header and the description box at their own size. */
static void test_review(void)
{
    KhdaysLowerLayout l = {0};
    numbered();
    rect(0, 159, DS_W, 160, 0x212121u); /* plain above the box */
    rect(8, 161, 248, 162, 0xffffffu);  /* the box's white lines */
    rect(8, 190, 248, 191, 0xffffffu);
    for (int r = 55; r < 152; r += 16) {
        rect(16, r, 230, r + 1, 0x5a5a5au); /* the rules */
        rect(16, r + 1, 230, r + 3, 0x212121u);
    }
    CHECK(lay(&l) == KHDAYS_LOWER_REVIEW);
    CHECK(shown(&l, 100.5f, 56) >= 5 && shown(&l, 100.5f, 136) >= 5); /* the rows under the rules repeated */
    CHECK(shown(&l, 100.5f, 10) == 1 && shown(&l, 100.5f, 175) == 1); /* header and box at their size */
    khdays_lower_free(&l);
}

static void test_short_display(void)
{
    KhdaysLowerLayout l = {0};
    int rows;
    field();
    khdays_lower_sample(&pic, ds, &rows);
    /* a display no taller than 4:3: the DS's shape */
    CHECK(khdays_lower_layout(&l, &pic, ds, rows, KHDAYS_LOWER_FIELD, OUT_W, DS_H * SCALE));
    CHECK(l.screen == KHDAYS_LOWER_NONE);
    khdays_lower_free(&l);
}

int main(void)
{
    test_unknown();
    test_field();
    test_load();
    test_load_short();
    test_characters();
    test_status();
    test_summary();
    test_missions();
    test_missions_grown();
    test_tutorials();
    test_diary();
    test_panels();
    test_plain();
    test_title();
    test_day();
    test_backdrop();
    test_fades();
    test_wider_display();
    test_shop();
    test_review();
    test_short_display();
    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("lower layout: all checks passed\n");
    return 0;
}
