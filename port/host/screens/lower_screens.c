/* lower_screens.h: the screens, each told by a few of its pixels (what holds while the game dims it too)
 * and laid out as docs/LOWER_DISPLAY.md shows. Rows and columns are the DS's; `extra` is how many rows the
 * taller picture has past the DS's 192. */
#include "lower_screens.h"
#include "lower_layout.h"
#include "lower_pixels.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The first row from `from` on that is one colour across, brighter than `min_sum` (a divider), or -1. */
static int divider(const uint32_t *ds, int from, int min_sum)
{
    for (int y = from; y < H; ++y) {
        if (sum(at(ds, 0, y)) > min_sum && uniform_row(ds, y)) {
            return y;
        }
    }
    return -1;
}
#define ICON_GREEN 40 /* the icons' green, dimmed too */

/* Mission Mode's L and R icons in the top corners: green, also dimmed under a dialog. */
static int green_icon(uint32_t c)
{
    return green(c) >= ICON_GREEN && green(c) > 2 * red(c) && green(c) > 2 * blue(c);
}
static int tab_icons(const uint32_t *ds)
{
    return green_icon(at(ds, 4, 6)) && green_icon(at(ds, 247, 6));
}

/* --- The field's INFORMATION panel (and the pause's, dimmed) ------------------------------------ */

#define FIELD_TOP 28    /* the header and the objective's line above */
#define FIELD_FOOT 148  /* the mission's bar (or the day's) from here */
#define FIELD_SIDE_L 24 /* the emblem pattern beside the panel */
#define FIELD_SIDE_R 232
#define FIELD_PATTERN 48 /* its height */
#define FIELD_BOXES 64   /* the boxes' rows from here ... */
#define FIELD_BOTTOM 140 /* ... to the panel's lower edge */
#define FIELD_PLAIN 100  /* a row of the panel's white beside the boxes */
#define TARGET_X0 32     /* the target's picture box */
#define TARGET_X1 97
#define TARGET_IN0 67
#define TARGET_IN1 137
#define TARGET_PLAIN 70 /* a row of its black */
#define MAP_X0 100      /* the map's box, its frame */
#define MAP_X1 232
#define MAP_IN_X0 104 /* inside the frame */
#define MAP_IN_X1 224
#define MAP_IN0 56
#define MAP_IN1 136
#define FIELD_EDGE 30      /* the panel's top edge, lilac */
#define FIELD_EDGE_MIN 8   /* its red, dimmed too (a mission fading in from black) */
#define FIELD_EDGE_TINT 2  /* its red and blue alike */
#define FIELD_WHITE_MIN 24 /* the line under the objective, dimmed too */
#define FIELD_PANEL_MIN 24 /* the panel's white beside its lilac edge, fading in too */

/* The panel's lilac top edge across row 30 and its white beside the boxes: there while the header is
 * still sliding in as a mission starts. */
static int field_panel(const uint32_t *ds)
{
    const uint32_t edge = at(ds, 128, FIELD_EDGE), plain = at(ds, 30, FIELD_PLAIN);
    return red(edge) >= FIELD_EDGE_MIN && red(edge) > green(edge) && blue(edge) > green(edge) &&
           abs(red(edge) - blue(edge)) <= FIELD_EDGE_TINT && at(ds, 40, FIELD_EDGE) == edge &&
           at(ds, 220, FIELD_EDGE) == edge && grey(plain) && red(plain) >= FIELD_PANEL_MIN &&
           at(ds, 220, FIELD_PLAIN) == plain;
}

static int tell_field(const uint32_t *ds)
{
    const uint32_t line = at(ds, 128, 23);
    return (grey(line) && red(line) >= FIELD_WHITE_MIN && uniform_row(ds, 23) && at(ds, 30, 100) == line &&
            sum(at(ds, 128, 14)) < 60) ||
           field_panel(ds);
}

static int map_field(const KhdaysLowerPlace *p, float ox, float oy, float *sx, float *sy)
{
    const float e = p->extra;
    *sx = ox;
    if (oy < FIELD_TOP) {
        *sy = oy;
    } else if (oy >= FIELD_FOOT + e) {
        *sy = oy - e;
    } else if (ox < FIELD_SIDE_L || ox >= FIELD_SIDE_R) {
        float y = oy;
        while (y >= FIELD_FOOT) {
            y -= FIELD_PATTERN;
        }
        *sy = y;
    } else if (ox >= MAP_X0 && ox < MAP_X1) {
        /* the map's box grows both ways, the map kept in its middle */
        const float m = oy - e / 2.0f;
        if (oy < MAP_IN0) {
            *sy = oy;
        } else if (oy >= MAP_IN1 + e) {
            *sy = oy - e;
        } else if (m >= MAP_IN0 && m < MAP_IN1) {
            *sy = m;
        } else if (ox < MAP_IN_X0 || ox >= MAP_IN_X1) {
            *sy = FIELD_PLAIN;
        } else {
            *sy = m < MAP_IN0 ? (float)MAP_IN0 : (float)(MAP_IN1 - 1);
        }
    } else if (oy < FIELD_BOXES) {
        *sy = oy;
    } else if (oy >= FIELD_BOTTOM + e) {
        *sy = oy - e;
    } else if (ox >= TARGET_X0 && ox < TARGET_X1) {
        const float m = oy - e / 2.0f;
        *sy = oy < TARGET_IN0                       ? oy
              : oy >= TARGET_IN1 + e                ? oy - e
              : (m >= TARGET_IN0 && m < TARGET_IN1) ? m
                                                    : (float)TARGET_PLAIN;
    } else {
        *sy = FIELD_PLAIN;
    }
    return 1;
}

/* --- Zoomed screens: the save list, the characters ---------------------------------------------- */

#define LOAD_TOP 16
#define LOAD_BLUE 2 /* the header line's blue over its red, at the least */
#define LOAD_ZOOM 1.17f
#define LOAD_CX 130.5f
#define CHARS_TOP 24
#define CHARS_ZOOM_MAX 1.2f
#define CHARS_FREE 4    /* DS columns kept free beside the grid */
#define CHARS_EDGE 90   /* a grid pixel differs this much (summed) from the backdrop at column 2 */
#define CHARS_STRIPES 4 /* the backdrop's stripes repeat every this many rows */
#define DIVIDER_SUM 150
#define WHITE_SUM 600
#define LINE_SUM 300        /* the white line under a page, dimmed under a dialog too */
#define DARK_SUM 150        /* the characters' title band */
#define CHARS_TITLE_BAND 20 /* a row of it black under the title */

static int tell_load(const uint32_t *ds)
{
    const uint32_t c = at(ds, 128, 14);
    /* row 15 black, the blue line above it: through a fade to black or to white the black stays no redder
     * than the blue line and the line stays blue */
    return uniform_row(ds, 14) && blue(c) > red(c) + LOAD_BLUE && grey(at(ds, 128, 15)) &&
           red(at(ds, 128, 15)) <= red(c);
}

/* The title's black band under "Select a Character" (the multiplayer groups' page shares the rest). */
static int characters_title(const uint32_t *ds)
{
    return sum(at(ds, 64, CHARS_TITLE_BAND)) == 0 && sum(at(ds, 100, CHARS_TITLE_BAND)) == 0;
}

static int tell_characters(const uint32_t *ds)
{
    return divider(ds, 170, WHITE_SUM) == 176 && !tab_icons(ds) && sum(at(ds, 128, 4)) < DARK_SUM &&
           characters_title(ds);
}

static int map_zoom(const KhdaysLowerPlace *p, float ox, float oy, float *sx, float *sy)
{
    const float mid_out = ((float)p->top + (float)p->foot + p->extra) / 2.0f;
    const float mid_src = ((float)p->top + (float)p->foot) / 2.0f;
    if (oy < (float)p->top) {
        *sx = ox;
        *sy = oy;
        return 1;
    }
    if (oy >= (float)p->foot + p->extra) {
        *sx = ox;
        *sy = oy - p->extra;
        return 1;
    }
    *sx = p->cx + (ox - KHDAYS_LOWER_DS_W / 2.0f) / p->zoom;
    *sy = mid_src + (oy - mid_out) / p->zoom;
    if (*sy >= (float)p->top && *sy < (float)p->foot) {
        return 1;
    }
    if (p->split == 0) {
        return 0; /* the save list: black around it */
    }
    /* the characters: the striped backdrop above the grid, again */
    *sx = ox;
    *sy = (float)(p->top + (int)oy % CHARS_STRIPES);
    return 1;
}

/* --- The title: its picture and menu over the copyright line ------------------------------------------ */

#define TITLE_FOOT 180 /* the copyright line's grey band from here to the bottom */
#define TITLE_TEXT 186 /* a row of the band beside its text */

/* The band one grey at its corners and middle, the same grey while the title fades in or out too. */
static int tell_title(const uint32_t *ds)
{
    const uint32_t band = at(ds, 0, TITLE_FOOT);
    return grey(band) && sum(band) > 0 && at(ds, 0, H - 1) == band && at(ds, W / 2, TITLE_FOOT) == band &&
           at(ds, W / 2, TITLE_FOOT + 1) == band && at(ds, 2, TITLE_TEXT) == band &&
           sum(at(ds, 0, TITLE_FOOT - 2)) > sum(band); /* the picture's white above it */
}

/* --- The camp's status panel ---------------------------------------------------------------------- */

#define STATUS_SPLIT 72 /* the red panel's columns, then the stats' */

static int tell_status(const uint32_t *ds)
{
    const uint32_t panel = at(ds, 30, 96), stats = at(ds, 150, 100);
    /* (dimmed as the menu closes too) */
    return red(panel) > 2 * green(panel) && red(panel) >= 8 && at(ds, 60, 100) == panel && grey(stats) &&
           red(stats) >= 3 && red(stats) <= 60 && red(stats) < red(panel) && sum(at(ds, 100, 120)) == 0;
}

/* --- A mission's summary ---------------------------------------------------------------------------- */

#define SUM_LEFT 8 /* the card, tabs and page span these columns ... */
#define SUM_RIGHT 248
#define SUM_FOOT 168        /* ... above the scanline band, the white line and Embark / Back */
#define SUM_SCAN 4          /* the band's scanlines, to fill above it */
#define SUM_TRACK 216       /* the scroll track from here */
#define SUM_TRACK_PLAIN 140 /* a row of it plain in every state */
#define SUM_TRACK_END 146   /* its lower end (the down arrow) from here */
#define SUM_LINE0 144       /* one ruled line of the page, added under the last ... */
#define SUM_LINE1 160
#define SUM_RULE 159     /* ... blank: the row under a rule (no text reaches it) to its rule */
#define SUM_ICON_ROWS 15 /* the L and R icons, at their own size */
#define SUM_ICON_COLS 16
#define SUM_CARD_LEFT 12 /* the card's red edges */
#define SUM_CARD_RIGHT 240
#define SUM_CARD_Y 24
#define SUM_CARD_RED 20  /* their red, dimmed too */
#define SUM_LINE_SUM 150 /* the white line under the page, dimmed under the dialog */
#define SUM_PAGE_MIN 12  /* the page's grey, dimmed too */

/* A point of the mission's card: red, dimmed under the Embark dialog too. */
static int card_red(uint32_t c)
{
    return red(c) >= SUM_CARD_RED && red(c) > 2 * green(c) && red(c) > 2 * blue(c);
}

/* The mission's red card, its left and right edges (Mission Mode's with the L and R icons, the story's
 * without), on every tab and under the Embark dialog. */
static int tell_summary(const uint32_t *ds)
{
    const uint32_t page = at(ds, 100, 120);
    return card_red(at(ds, SUM_CARD_LEFT, SUM_CARD_Y)) && card_red(at(ds, SUM_CARD_RIGHT, SUM_CARD_Y)) &&
           divider(ds, 170, SUM_LINE_SUM) == 176 && grey(page) && red(page) >= SUM_PAGE_MIN && at(ds, 30, 100) == page;
}

static int map_summary(const KhdaysLowerPlace *p, float ox, float oy, float *sx, float *sy)
{
    const float foot_top = p->out_h - (float)(H - SUM_FOOT);
    const float page_rows = (float)(SUM_FOOT + SUM_LINE1 - SUM_LINE0) * p->zoom;
    if (p->split && oy < SUM_ICON_ROWS && (ox < SUM_ICON_COLS || ox >= KHDAYS_LOWER_DS_W - SUM_ICON_COLS)) {
        *sx = ox;
        *sy = oy;
    } else if (oy >= foot_top) {
        *sx = ox;
        *sy = (float)SUM_FOOT + (oy - foot_top);
    } else if (oy < page_rows) {
        const float k = oy / p->zoom;
        const int row = (int)k;
        *sx = (float)SUM_LEFT + ox / p->zoom;
        *sy = (float)p->rows[*sx >= SUM_TRACK ? 1 : 0][row] + (k - (float)row);
    } else {
        *sx = ox;
        *sy = (float)(SUM_FOOT + (int)oy % SUM_SCAN);
    }
    return 1;
}

/* --- A list of missions (Mission Mode's, the story's day by day) --------------------------------------- */

#define LIST_FRAME_BOTTOM 176 /* the frame's lower border from here: the list grows down to it */
#define LIST_SIDE_ROW 100     /* a row through the frame's sides and the scroll track */
#define LIST_PLAIN0 20        /* the frame's sides and the track from here (the rows they grow by: plain ones) */
#define LIST_IN0 16           /* the entries' columns (the frame leaves its own out) */
#define LIST_IN1 222
#define LIST_TRACK0 222 /* the scroll track: its rounded end moves to its new bottom */
#define LIST_TRACK1 243
#define LIST_TRACK_END 173
#define LIST_SHIFT 9    /* the left margin left out: the frame's left edge on the screen's */
#define LIST_SEAM 191   /* plain from top to bottom, between the names and the crowns ... */
#define LIST_WIDEN 20   /* ... repeated: the frame's right edge (244) on the screen's */
#define LIST_ARROW0 114 /* the down arrow under the list, kept in its middle */
#define LIST_ARROW1 126
#define LIST_ARROW_MOVE 1   /* the list's middle moves this far */
#define LIST_BORDER_COL 100 /* a column of the lower border without the arrow */
#define LIST_ICON_ROWS 16   /* the L and R icons at their own size and place */
#define LIST_ICON_COLS 16
#define LIST_GREY 108     /* the frame and the scroll track ... */
#define LIST_END_GREY 149 /* ... and the track's rounded end: as bright as each other as these, dimmed too */
#define LIST_GREY_NEAR 8  /* (in 149ths of the track's: the Thor draws 107 and 148, the tutorials 109 and 150) */
#define LIST_GREY_MIN 20
#define LIST_EDGE_ROW0 60 /* rows its left edge is looked for at, as well as LIST_SIDE_ROW */
#define LIST_EDGE_ROW1 140
#define LAYER_BG1 (1u << 9) /* compose.h: KDF_BG_ON_SHIFT + 1 */
#define LAYER_OBJ 0x10u     /* KDF_OBJ_ON */

/* The list's frame and scroll track as both lists draw them (the tutorials' too, told apart by their
 * header: its frame shares its entries' layer, and every tutorial already shows). */
/* The frame's left edge at row y: the pointing hand covers it beside the chosen entry. */
static int list_edge(const uint32_t *ds, int y, uint32_t track)
{
    const uint32_t edge = at(ds, 10, y);
    return at(ds, 9, y) != edge && edge == track;
}

static int tell_missions(const uint32_t *ds)
{
    const uint32_t track = at(ds, 240, 150), end = at(ds, 240, 173);
    return grey(track) && red(track) >= LIST_GREY_MIN && grey(end) &&
           abs(red(end) * LIST_GREY - red(track) * LIST_END_GREY) <= red(track) * LIST_GREY_NEAR &&
           at(ds, 232, 175) == track &&
           (list_edge(ds, LIST_EDGE_ROW0, track) || list_edge(ds, LIST_SIDE_ROW, track) ||
            list_edge(ds, LIST_EDGE_ROW1, track)) &&
           !uniform_row(ds, 14);
}

static float list_column(float ox)
{
    if (ox <= (float)(LIST_SEAM - LIST_SHIFT)) {
        return ox + LIST_SHIFT;
    }
    if (ox <= (float)(LIST_SEAM - LIST_SHIFT + LIST_WIDEN)) {
        return (float)LIST_SEAM + 0.5f;
    }
    return ox + (float)(LIST_SHIFT - LIST_WIDEN);
}

static int map_missions(const KhdaysLowerPlace *p, float ox, float oy, float *sx, float *sy)
{
    const float e = p->extra;
    if (oy < LIST_ICON_ROWS && (ox < LIST_ICON_COLS || ox >= KHDAYS_LOWER_DS_W - LIST_ICON_COLS)) {
        *sx = ox;
        *sy = oy;
        return 1;
    }
    *sx = list_column(ox);
    if (oy >= (float)LIST_FRAME_BOTTOM + e) {
        /* the lower border, the down arrow in the widened list's middle */
        *sy = oy - e;
        if (ox >= (float)(LIST_ARROW0 + LIST_ARROW_MOVE) && ox < (float)(LIST_ARROW1 + LIST_ARROW_MOVE)) {
            *sx = ox - LIST_ARROW_MOVE;
        } else if (*sx >= (float)LIST_ARROW0 && *sx < (float)LIST_ARROW1) {
            *sx = (float)LIST_BORDER_COL + 0.5f;
        }
    } else if (*sx >= (float)LIST_TRACK0 && *sx < (float)LIST_TRACK1 && oy >= (float)LIST_TRACK_END) {
        *sy = oy < (float)LIST_TRACK_END + e ? (float)p->foot : oy - e; /* the track's plain row */
    } else if (oy < (float)LIST_FRAME_BOTTOM) {
        *sy = oy;
    } else if (*sx >= (float)LIST_IN0 && *sx < (float)LIST_IN1) {
        *sy = (float)KHDAYS_LOWER_ROWS + oy; /* the next missions, as the game drew them under its frame */
    } else {
        *sy = (float)p->top + (oy - (float)(int)oy); /* the frame's sides, a plain row of them */
    }
    return 1;
}

/* ---------------------------------------------------------------------------------------------------- */

int khdays_lower_screens_tell(const uint32_t *ds, int ds_rows)
{
    (void)ds_rows;
    if (tell_field(ds)) {
        return KHDAYS_LOWER_FIELD;
    }
    if (tell_load(ds)) {
        return KHDAYS_LOWER_LOAD;
    }
    if (tell_summary(ds)) {
        return KHDAYS_LOWER_SUMMARY;
    }
    if (tell_missions(ds)) {
        return KHDAYS_LOWER_MISSIONS;
    }
    if (tell_characters(ds)) {
        return KHDAYS_LOWER_CHARACTERS;
    }
    if (tell_status(ds)) {
        return KHDAYS_LOWER_STATUS;
    }
    if (tell_title(ds)) {
        return KHDAYS_LOWER_TITLE;
    }
    return khdays_lower_pages_tell(ds); /* the lists drawn a page at a time, a fade (lower_pages.c) */
}

int khdays_lower_screens_place(int screen, const uint32_t *ds, int ds_rows, float out_h, KhdaysLowerPlace *place,
                               int *key)
{
    static const int status_left[][2] = {{82, 6}, {98, 6}, {114, 6}, {130, 6}, {150, 7}};
    static const int status_right[][2] = {{12, 4}, {28, 4}, {44, 4}, {60, 4}, {76, 4}, {92, 4}, {106, 4}, {124, 3}};
    const int rows = (int)ceilf(out_h);
    (void)ds_rows;
    memset(place, 0, sizeof(*place));
    place->out_h = out_h;
    place->extra = out_h - (float)H;
    *key = 0;
    if (place->extra <= 0.0f || rows > KHDAYS_LOWER_ROWS_MAX) {
        return 0;
    }
    switch (screen) {
    case KHDAYS_LOWER_FIELD: place->map = map_field; return 1;
    case KHDAYS_LOWER_LOAD: {
        const int line = divider(ds, 20, DIVIDER_SUM);
        place->map = map_zoom;
        place->top = LOAD_TOP;
        place->foot = line > LOAD_TOP ? line - 1 : H;
        place->zoom = LOAD_ZOOM;
        place->cx = LOAD_CX;
        *key = place->foot;
        return 1;
    }
    case KHDAYS_LOWER_CHARACTERS: {
        const int line = divider(ds, 170, WHITE_SUM);
        int c0 = W, c1 = -1;
        float half, zoom;
        place->map = map_zoom;
        place->split = 1; /* the backdrop around the grid */
        place->top = CHARS_TOP;
        place->foot = line > CHARS_TOP ? line - 1 : H;
        for (int y = place->top; y < place->foot; ++y) {
            for (int x = 0; x < W; ++x) {
                const uint32_t c = at(ds, x, y), b = at(ds, 2, y);
                const int d = (red(c) > red(b) ? red(c) - red(b) : red(b) - red(c)) +
                              (green(c) > green(b) ? green(c) - green(b) : green(b) - green(c)) +
                              (blue(c) > blue(b) ? blue(c) - blue(b) : blue(b) - blue(c));
                if (d > CHARS_EDGE) {
                    c0 = x < c0 ? x : c0;
                    c1 = x > c1 ? x : c1;
                }
            }
        }
        place->cx = W / 2.0f;
        half = c1 < 0 ? W / 2.0f
                      : (W / 2.0f - (float)c0 > (float)c1 + 1.0f - W / 2.0f ? W / 2.0f - (float)c0
                                                                            : (float)c1 + 1.0f - W / 2.0f) +
                            CHARS_FREE;
        zoom = (W / 2.0f) / half;
        zoom = zoom < CHARS_ZOOM_MAX ? zoom : CHARS_ZOOM_MAX;
        zoom = zoom < ((float)(place->foot - place->top) + place->extra) / (float)(place->foot - place->top)
                   ? zoom
                   : ((float)(place->foot - place->top) + place->extra) / (float)(place->foot - place->top);
        place->zoom = zoom < 1.0f ? 1.0f : zoom;
        *key = place->foot * 1000 + (int)(place->zoom * 100.0f);
        return 1;
    }
    case KHDAYS_LOWER_TITLE:
        /* the picture enlarged to fill the rows above the band, its left edge kept (the menu's squares and
         * words); the right edge's scenery past the display left out */
        place->map = map_zoom;
        place->top = 0;
        place->foot = TITLE_FOOT;
        place->zoom = ((float)TITLE_FOOT + place->extra) / (float)TITLE_FOOT;
        place->cx = (float)W / 2.0f / place->zoom;
        return 1;
    case KHDAYS_LOWER_STATUS:
        place->map = khdays_lower_map_rows;
        place->split = STATUS_SPLIT;
        khdays_lower_spread(place->rows[0], rows, status_left, (int)(sizeof(status_left) / sizeof(status_left[0])));
        khdays_lower_spread(place->rows[1], rows, status_right, (int)(sizeof(status_right) / sizeof(status_right[0])));
        return 1;
    case KHDAYS_LOWER_MISSIONS:
        place->map = map_missions;
        place->row0 = LIST_FRAME_BOTTOM;
        place->row_count = (int)ceilf(place->extra) + 1;
        place->layers_off = LAYER_BG1 | LAYER_OBJ; /* the frame and the sprites: the entries alone */
        place->top = khdays_lower_plain_row(ds, LIST_PLAIN0, LIST_TRACK_END, 0, LIST_IN0);
        place->foot = khdays_lower_plain_row(ds, LIST_PLAIN0, LIST_TRACK_END, LIST_TRACK0, LIST_TRACK1);
        *key = place->top * 1000 + place->foot;
        return 1;
    case KHDAYS_LOWER_SUMMARY: {
        int n = 0;
        place->map = map_summary;
        place->split = tab_icons(ds); /* Mission Mode's L and R icons, kept at their own size */
        *key = place->split;
        place->zoom = (float)W / (float)(SUM_RIGHT - SUM_LEFT);
        if ((float)(SUM_FOOT + SUM_LINE1 - SUM_LINE0) * place->zoom > out_h - (float)(H - SUM_FOOT)) {
            return 0; /* a display too short for the added line */
        }
        int t = 0;
        for (int y = 0; y < SUM_FOOT; ++y) {
            if (y == SUM_LINE1) {
                for (int k = SUM_LINE0; k < SUM_LINE1; ++k) {
                    place->rows[0][n++] = (int16_t)(k == SUM_RULE ? SUM_RULE : SUM_LINE0);
                }
            }
            if (y == SUM_TRACK_END) {
                for (int k = SUM_LINE0; k < SUM_LINE1; ++k) {
                    place->rows[1][t++] = (int16_t)SUM_TRACK_PLAIN; /* the track's added rows, above its end */
                }
            }
            place->rows[0][n++] = (int16_t)y;
            place->rows[1][t++] = (int16_t)y;
        }
        return 1;
    }
    default: return khdays_lower_pages_place(screen, ds, rows, place, key);
    }
}
