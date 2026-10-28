/* lower_pixels.h: the lists the game draws a page at a time (the tutorials', Roxas's Diary's, the Panels
 * page's stock) and a picture of one colour, and the rows the screens are laid out by (docs/LOWER_DISPLAY.md). */
#include "lower_pixels.h"

#include <stdlib.h>

void khdays_lower_spread(int16_t *out, int rows, const int (*inserts)[2], int count)
{
    int weights = 0, weighed = 0, given = 0, n = 0;
    for (int i = 0; i < count; ++i) {
        weights += inserts[i][1];
    }
    for (int y = 0, i = 0; y < H && n < rows; ++y) {
        if (i < count && inserts[i][0] == y) {
            /* each its share rounded, as the shares so far add up to: the rows apart evenly */
            const int extra = rows - H;
            int share;
            weighed += inserts[i][1];
            share = (extra * weighed + weights / 2) / weights - given;
            for (int k = 0; k < share && n < rows; ++k) {
                out[n++] = (int16_t)y;
            }
            given += share;
            ++i;
        }
        out[n++] = (int16_t)y;
    }
    while (n < rows) {
        out[n] = out[n - 1];
        ++n;
    }
}

int khdays_lower_map_rows(const KhdaysLowerPlace *p, float ox, float oy, float *sx, float *sy)
{
    const int y = (int)oy;
    const int band = ox < (float)p->split ? 0 : p->split2 > 0 && ox >= (float)p->split2 ? 2 : 1;
    *sx = ox;
    *sy = (float)p->rows[band][y < KHDAYS_LOWER_ROWS_MAX ? y : KHDAYS_LOWER_ROWS_MAX - 1] + (oy - (float)y);
    return 1;
}

int khdays_lower_plain_row(const uint32_t *ds, int y0, int y1, int x0, int x1)
{
    uint32_t hash[H];
    int best = y0, best_count = 0;
    for (int y = y0; y < y1; ++y) {
        uint32_t h = 2166136261u;
        for (int x = x0; x < x1; ++x) {
            h = (h ^ at(ds, x, y)) * 16777619u;
        }
        hash[y] = h;
    }
    for (int y = y0; y < y1; ++y) {
        int count = 0;
        for (int k = y0; k < y1; ++k) {
            count += hash[k] == hash[y];
        }
        if (count > best_count) {
            best = y;
            best_count = count;
        }
    }
    return best;
}

/* --- Lists drawn a page at a time (the tutorials', the diary's, the Panels page's stock) ---------------
 * The game draws only the entries it shows, so the extra rows space them apart: a plain row between
 * entries repeated (map_rows); the scroll track grows at a plain row of its own. */

#define TUT_HEADER 14     /* the header's red line across */
#define TUT_FRAME_DARK 16 /* the frame's stripes, then its top edge */
#define TUT_FRAME_EDGE 26
#define TUT_GAP0 48 /* the rows between entries: from here, every 16 */
#define TUT_GAPS 8
#define TUT_TRACK 224 /* the scroll track from here */
#define TUT_TRACK0 32 /* its rows */
#define TUT_TRACK1 175
#define DIARY_STRIPE0 0 /* the backdrop's stripes above and below the list ... */
#define DIARY_STRIPE1 2
#define DIARY_BELOW 188
#define DIARY_PLAIN 12 /* ... its plain green between (the scroll arrows cross it in the middle) */
#define DIARY_ARROW0 112
#define DIARY_ARROW1 132
#define DIARY_GAP0 32
#define DIARY_GAPS 9
#define DIARY_TRACK 232
#define DIARY_TRACK0 16
#define DIARY_TRACK1 175
#define PANELS_LIST 95   /* the board left of here, the stock from here ... */
#define PANELS_TRACK 232 /* ... its scroll track from here */
#define PANELS_ENTRY0 24 /* the stock's entries, 16 rows each, their first row plain */
#define PANELS_ENTRIES 8
#define PANELS_BOARD_ABOVE 22 /* the board's frame, plain above its grid and below */
#define PANELS_BOARD_BELOW 152
#define PANELS_TRACK1 151
#define PANELS_FRAME 160    /* the dark line over the help, ... */
#define PANELS_HELP 161     /* ... the help box's white top and ... */
#define PANELS_HELP_END 190 /* ... bottom lines (their rounded ends left out) */
#define PANELS_SPAN0 8
#define PANELS_SPAN1 248
#define PANELS_BOARD_X 4
#define PANELS_STOCK_X 150
#define ENTRY_ROWS 16
#define TUT_HEADER_RED 40   /* the header's red, dimmed too */
#define PANELS_HELP_SUM 120 /* the help box's white, dimmed too */

static int tell_tutorials(const uint32_t *ds)
{
    const uint32_t header = at(ds, 0, TUT_HEADER), dark = at(ds, 30, TUT_FRAME_DARK), edge = at(ds, 30, TUT_FRAME_EDGE);
    return uniform_row(ds, TUT_HEADER) && red(header) >= TUT_HEADER_RED && red(header) > 2 * green(header) &&
           sum(at(ds, 128, TUT_HEADER + 1)) == 0 && at(ds, 200, TUT_FRAME_DARK) == dark &&
           at(ds, 200, TUT_FRAME_EDGE) == edge && grey(edge) && sum(edge) > sum(dark);
}

static int tell_diary(const uint32_t *ds)
{
    const uint32_t plain = at(ds, 0, DIARY_PLAIN), stripe = at(ds, 0, DIARY_STRIPE0);
    return uniform_row(ds, DIARY_STRIPE0) && uniform_row(ds, DIARY_STRIPE1) &&
           uniform_span(ds, DIARY_PLAIN, 0, DIARY_ARROW0) && uniform_span(ds, DIARY_PLAIN, DIARY_ARROW1, W) &&
           at(ds, DIARY_ARROW1, DIARY_PLAIN) == plain && uniform_row(ds, DIARY_BELOW) &&
           at(ds, 0, DIARY_BELOW) == stripe && green(plain) > red(plain) && green(plain) > blue(plain) &&
           sum(plain) > sum(stripe);
}

static int tell_panels(const uint32_t *ds)
{
    const uint32_t help = at(ds, PANELS_SPAN0, PANELS_HELP), board = at(ds, PANELS_BOARD_X, 100),
                   stock = at(ds, PANELS_STOCK_X, PANELS_BOARD_BELOW);
    return uniform_span(ds, PANELS_HELP, PANELS_SPAN0, PANELS_SPAN1) && sum(help) > PANELS_HELP_SUM &&
           uniform_span(ds, PANELS_HELP_END, PANELS_SPAN0, PANELS_SPAN1) &&
           at(ds, PANELS_SPAN0, PANELS_HELP_END) == help &&
           uniform_span(ds, PANELS_FRAME, PANELS_SPAN0, PANELS_SPAN1) &&
           sum(at(ds, PANELS_SPAN0, PANELS_FRAME)) < sum(help) && at(ds, PANELS_BOARD_X, 60) == board &&
           at(ds, PANELS_BOARD_X, 140) == board && red(board) == green(board) && blue(board) > red(board) &&
           blue(stock) > red(stock) * 3 / 2 && blue(stock) > green(stock); /* dimmed as it closes too */
}

/* --- A day's missions (the story's): the day's card above, four slots of the list ------------------------
 * Only the missions shown are drawn: the slots are spaced apart, the rows between them repeated alike; the
 * scroll track longer at a plain row of its own. */

#define DAY_CARD_Y 20  /* the day's red card */
#define DAY_FRAME_Y 40 /* the list frame's top line, one dark grey across */
#define DAY_FRAME_X0 16
#define DAY_FRAME_X1 244
#define DAY_TRACK 224   /* the scroll track from here, ... */
#define DAY_TRACK_X 230 /* ... its light grey at this column */
#define DAY_EDGE_X 244  /* the frame's right edge */
#define DAY_TRACK0 48
#define DAY_TRACK1 175
#define DAY_GAP0 48 /* the rows the slots are spaced at: above the first, between, below the last */
#define DAY_GAPS 5
#define DAY_GAP_STEP 32
#define DAY_RED_MIN 8 /* the card's red, dimmed too */

static int day_red(const uint32_t *ds, int x)
{
    const uint32_t c = at(ds, x, DAY_CARD_Y);
    return red(c) > 2 * green(c) && red(c) > 2 * blue(c) && red(c) >= DAY_RED_MIN;
}

static int tell_day(const uint32_t *ds)
{
    const uint32_t track = at(ds, DAY_TRACK_X, 100);
    return day_red(ds, 20) && day_red(ds, 100) && day_red(ds, 230) &&
           uniform_span(ds, DAY_FRAME_Y, DAY_FRAME_X0, DAY_FRAME_X1) && grey(at(ds, DAY_FRAME_X0, DAY_FRAME_Y)) &&
           sum(track) > sum(at(ds, DAY_EDGE_X, 100)) && blue(track) >= red(track) && red(track) == green(track);
}

/* --- A mission's review: Rewards and Items Collected, each a list of ruled rows ------------------------
 * The rows apart (the plain row under each rule repeated alike), the scroll track longer at a plain row. */

#define REVIEW_BOX_TOP 161 /* the description box's white lines */
#define REVIEW_BOX_BOTTOM 190
#define REVIEW_ABOVE_BOX 159 /* plain dark across above the box */
#define REVIEW_RULE 55       /* the lists' first rule, then every 16 rows */
#define REVIEW_RULE_X0 60    /* a column of the left list's rules and one of the right's */
#define REVIEW_RULE_X1 180
#define REVIEW_GAP0 56 /* the rows under the rules, repeated */
#define REVIEW_GAPS 6
#define REVIEW_TRACK 236 /* the scroll track from here */
#define REVIEW_TRACK0 48
#define REVIEW_TRACK1 147
#define REVIEW_MARGIN 8

static int tell_review(const uint32_t *ds)
{
    const uint32_t line = at(ds, REVIEW_MARGIN, REVIEW_BOX_TOP), dark = at(ds, 0, REVIEW_ABOVE_BOX),
                   rule = at(ds, REVIEW_RULE_X0, REVIEW_RULE);
    return uniform_span(ds, REVIEW_BOX_TOP, REVIEW_MARGIN, W - REVIEW_MARGIN) &&
           uniform_span(ds, REVIEW_BOX_BOTTOM, REVIEW_MARGIN, W - REVIEW_MARGIN) &&
           at(ds, REVIEW_MARGIN, REVIEW_BOX_BOTTOM) == line && grey(line) && uniform_row(ds, REVIEW_ABOVE_BOX) &&
           grey(dark) && sum(line) > sum(dark) && grey(rule) && sum(rule) > sum(dark) &&
           at(ds, REVIEW_RULE_X1, REVIEW_RULE) == rule && at(ds, REVIEW_RULE_X0, REVIEW_RULE + ENTRY_ROWS) == rule &&
           at(ds, REVIEW_RULE_X0, REVIEW_RULE + 1) == at(ds, REVIEW_RULE_X0, REVIEW_RULE + 2);
}

/* --- A picture on a plain backdrop: its top and bottom rows one colour (a logo on white) ----------------- */

#define BACKDROP_ROWS 8 /* this many rows at the top and the bottom, one colour */

static int tell_backdrop(const uint32_t *ds)
{
    const uint32_t c = at(ds, 0, 0);
    if (sum(c) == 0) {
        return 0; /* black: the bars are already */
    }
    for (int y = 0; y < BACKDROP_ROWS; ++y) {
        if (!uniform_row(ds, y) || !uniform_row(ds, H - 1 - y) || at(ds, 0, y) != c || at(ds, 0, H - 1 - y) != c) {
            return 0;
        }
    }
    return 1;
}

/* The DS's shape in the middle, the rows above and below it its own plain top and bottom rows again. */
static int map_backdrop(const KhdaysLowerPlace *p, float ox, float oy, float *sx, float *sy)
{
    const float y = oy - p->extra / 2.0f;
    *sx = ox;
    *sy = y < 0.0f ? 0.0f : y >= (float)H ? (float)(H - 1) : y;
    return 1;
}

/* --- One colour across (a fade to white): all of the display that colour -------------------------------- */

#define PLAIN_NEAR 2 /* each channel within this of the first pixel's (the picture's scaling blends a level) */

static int tell_plain(const uint32_t *ds)
{
    const uint32_t c = at(ds, 0, 0);
    if (sum(c) == 0) {
        return 0; /* black: the bars are already */
    }
    for (int i = 0; i < W * H; ++i) {
        if (abs(red(ds[i]) - red(c)) > PLAIN_NEAR || abs(green(ds[i]) - green(c)) > PLAIN_NEAR ||
            abs(blue(ds[i]) - blue(c)) > PLAIN_NEAR) {
            return 0;
        }
    }
    return 1;
}

static int map_plain(const KhdaysLowerPlace *p, float ox, float oy, float *sx, float *sy)
{
    *sx = ox;
    *sy = oy * (float)H / p->out_h;
    return 1;
}

/* Inserts every ENTRY_ROWS rows from `row0`, `count` of them, each as heavy. */
static int every_entry(int (*inserts)[2], int row0, int count)
{
    for (int i = 0; i < count; ++i) {
        inserts[i][0] = row0 + i * ENTRY_ROWS;
        inserts[i][1] = 1;
    }
    return count;
}

int khdays_lower_pages_tell(const uint32_t *ds)
{
    if (tell_tutorials(ds)) {
        return KHDAYS_LOWER_TUTORIALS;
    }
    if (tell_diary(ds)) {
        return KHDAYS_LOWER_DIARY;
    }
    if (tell_panels(ds)) {
        return KHDAYS_LOWER_PANELS;
    }
    if (tell_day(ds)) {
        return KHDAYS_LOWER_DAY;
    }
    if (tell_review(ds)) {
        return KHDAYS_LOWER_REVIEW;
    }
    if (tell_plain(ds)) {
        return KHDAYS_LOWER_PLAIN;
    }
    if (tell_backdrop(ds)) {
        return KHDAYS_LOWER_BACKDROP;
    }
    return KHDAYS_LOWER_NONE;
}

int khdays_lower_pages_place(int screen, const uint32_t *ds, int rows, KhdaysLowerPlace *place, int *key)
{
    switch (screen) {
    case KHDAYS_LOWER_TUTORIALS:
    case KHDAYS_LOWER_DIARY: {
        const int tut = screen == KHDAYS_LOWER_TUTORIALS;
        int gaps[DIARY_GAPS][2], track[1][2] = {{0, 1}};
        place->map = khdays_lower_map_rows;
        place->split = tut ? TUT_TRACK : DIARY_TRACK;
        track[0][0] = tut ? khdays_lower_plain_row(ds, TUT_TRACK0, TUT_TRACK1, TUT_TRACK, W)
                          : khdays_lower_plain_row(ds, DIARY_TRACK0, DIARY_TRACK1, DIARY_TRACK, W);
        khdays_lower_spread(place->rows[0], rows, (const int(*)[2])gaps,
                            every_entry(gaps, tut ? TUT_GAP0 : DIARY_GAP0, tut ? TUT_GAPS : DIARY_GAPS));
        khdays_lower_spread(place->rows[1], rows, (const int(*)[2])track, 1);
        *key = track[0][0];
        return 1;
    }
    case KHDAYS_LOWER_PANELS: {
        static const int board[][2] = {{PANELS_BOARD_ABOVE, 1}, {PANELS_BOARD_BELOW, 1}};
        int entries[PANELS_ENTRIES][2], track[1][2] = {{0, 1}};
        place->map = khdays_lower_map_rows;
        place->split = PANELS_LIST;
        place->split2 = PANELS_TRACK;
        track[0][0] = khdays_lower_plain_row(ds, PANELS_ENTRY0, PANELS_TRACK1, PANELS_TRACK, W);
        khdays_lower_spread(place->rows[0], rows, board, 2);
        khdays_lower_spread(place->rows[1], rows, (const int(*)[2])entries,
                            every_entry(entries, PANELS_ENTRY0, PANELS_ENTRIES));
        khdays_lower_spread(place->rows[2], rows, (const int(*)[2])track, 1);
        *key = track[0][0];
        return 1;
    }
    case KHDAYS_LOWER_DAY: {
        int gaps[DAY_GAPS][2], track[1][2] = {{0, 1}};
        place->map = khdays_lower_map_rows;
        place->split = DAY_TRACK;
        for (int i = 0; i < DAY_GAPS; ++i) {
            gaps[i][0] = DAY_GAP0 + i * DAY_GAP_STEP;
            gaps[i][1] = 1;
        }
        track[0][0] = khdays_lower_plain_row(ds, DAY_TRACK0, DAY_TRACK1, DAY_TRACK, W);
        khdays_lower_spread(place->rows[0], rows, (const int(*)[2])gaps, DAY_GAPS);
        khdays_lower_spread(place->rows[1], rows, (const int(*)[2])track, 1);
        *key = track[0][0];
        return 1;
    }
    case KHDAYS_LOWER_REVIEW: {
        int gaps[REVIEW_GAPS][2], track[1][2] = {{0, 1}};
        place->map = khdays_lower_map_rows;
        place->split = REVIEW_TRACK;
        for (int i = 0; i < REVIEW_GAPS; ++i) {
            gaps[i][0] = REVIEW_GAP0 + i * ENTRY_ROWS;
            gaps[i][1] = 1;
        }
        track[0][0] = khdays_lower_plain_row(ds, REVIEW_TRACK0, REVIEW_TRACK1, REVIEW_TRACK, W);
        khdays_lower_spread(place->rows[0], rows, (const int(*)[2])gaps, REVIEW_GAPS);
        khdays_lower_spread(place->rows[1], rows, (const int(*)[2])track, 1);
        *key = track[0][0];
        return 1;
    }
    case KHDAYS_LOWER_PLAIN: place->map = map_plain; return 1;
    case KHDAYS_LOWER_BACKDROP: place->map = map_backdrop; return 1;
    default: return 0;
    }
}
