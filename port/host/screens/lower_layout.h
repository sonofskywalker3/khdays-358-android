/* The lower screen as tall as a second display that is taller than 4:3 (the Thor's lower one, 1240 x
 * 1080), never stretched: each screen the port knows is laid out again in the taller picture -- its
 * pieces moved to the edges, a list or a page grown with the rows the game drew past the DS's 192, a
 * region enlarged both ways, rows or columns of a plain stretch repeated -- and a screen it does not
 * know keeps the DS's shape with black bars. The screen is told by a few of its pixels. Plain C: the
 * PC's tests run it on recorded frames (tests/lower_layout_test.c). */
#ifndef KHDAYS_LOWER_LAYOUT_H
#define KHDAYS_LOWER_LAYOUT_H

#include <stdint.h>

#define KHDAYS_LOWER_DS_W 256
#define KHDAYS_LOWER_DS_H 192
#define KHDAYS_LOWER_OVERSCAN 32 /* (the DS's rows sampled are the screen's 192 alone) */
#define KHDAYS_LOWER_ROWS 1000   /* a place's row from here on: the extra rows' (lower_screens.h), DS row - this */

/* The screens laid out (KHDAYS_LOWER_NONE: the DS's shape, centred, black bars). */
enum {
    KHDAYS_LOWER_NONE,
    KHDAYS_LOWER_FIELD,      /* the field's INFORMATION panel (and the pause's, dimmed) */
    KHDAYS_LOWER_LOAD,       /* the save list: Load, Save, their dialogs */
    KHDAYS_LOWER_CHARACTERS, /* Mission Mode's character select */
    KHDAYS_LOWER_STATUS,     /* the camp's status panel */
    KHDAYS_LOWER_MISSIONS,   /* a list of missions: Mission Mode's, the story's day by day */
    KHDAYS_LOWER_SUMMARY,    /* a mission's summary, objectives and rewards */
    KHDAYS_LOWER_TUTORIALS,  /* the tutorials' list */
    KHDAYS_LOWER_DIARY,      /* Roxas's Diary's list */
    KHDAYS_LOWER_PANELS,     /* the Panels page: the board and the stock */
    KHDAYS_LOWER_PLAIN,      /* one colour across (a fade to white): all of the display that colour */
    KHDAYS_LOWER_TITLE,      /* the title's picture and menu over the copyright line */
    KHDAYS_LOWER_DAY,        /* a day's missions (the story's), under the day's card */
    KHDAYS_LOWER_BACKDROP,   /* a picture on a plain backdrop (a logo on white): the bars that colour */
    KHDAYS_LOWER_LAYOUTS
};

/* The lower picture as it arrives: RGBA rows, the DS's 256 x 192 scaled to width x height; `overscan`
 * (or NULL) extra rows the layout asked for at the same scale, `overscan_rows` of them, from DS row
 * `overscan_row0` on, composed with some layers left out (a list's next entry, under its frame). */
typedef struct KhdaysLowerPicture {
    const uint8_t *rgba;
    int width, height, pitch;
    const uint8_t *overscan;
    int overscan_rows, overscan_pitch, overscan_row0;
} KhdaysLowerPicture;

/* A layout made for a picture and a display: for each display pixel the picture's pixel it shows (an
 * index into the picture's rows then the overscan's, -1 black). Made again only when the screen or
 * what was measured on it changes. */
typedef struct KhdaysLowerLayout {
    int screen;        /* KHDAYS_LOWER_* */
    int key;           /* the screen's measurements, folded: a change makes the map again */
    int width, height; /* the display */
    int src_w, src_h;  /* the picture it was made for */
    int32_t *map;      /* width x height */
    int map_size;
    int rows_got; /* the extra rows it was made with (their height in the picture) */
    /* the extra rows the screen wants (renderer: gfx_render.h khdays_gfx_render_set_lower_rows): DS rows
     * from want_row0, want_rows of them (0 none), composed with want_off's layers left out */
    int want_row0, want_rows;
    uint32_t want_off;
} KhdaysLowerLayout;

/* The DS's pixels of the picture at their middles, 0x00RRGGBB, row by row: what the screens are told
 * by. `ds` holds KHDAYS_LOWER_DS_W x (KHDAYS_LOWER_DS_H + KHDAYS_LOWER_OVERSCAN); *ds_rows is 192. */
void khdays_lower_sample(const KhdaysLowerPicture *picture, uint32_t *ds, int *ds_rows);

/* Which screen the DS's pixels show (KHDAYS_LOWER_NONE when none it knows). */
int khdays_lower_screen(const uint32_t *ds, int ds_rows);

/* Makes `layout` for the picture on a display width x height (when the screen or its measurements
 * changed); 0 when it cannot (no memory): the caller shows the picture as it is. */
int khdays_lower_layout(KhdaysLowerLayout *layout, const KhdaysLowerPicture *picture, const uint32_t *ds, int ds_rows,
                        int screen, int width, int height);

/* Writes the display's pixels (RGBA, `pitch` bytes a row) from the picture through the layout. */
void khdays_lower_draw(const KhdaysLowerLayout *layout, const KhdaysLowerPicture *picture, uint8_t *out, int pitch);

/* The DS pixel a display pixel shows (a touch there): 0 when it shows none of the DS's screen (black,
 * or a row past the DS's 192). */
int khdays_lower_touch(const KhdaysLowerLayout *layout, int x, int y, int *ds_x, int *ds_y);

/* The extra rows to ask the renderer for on a display width x height while no screen wants others (the
 * mission lists'), so a list shows laid out from its first frame. */
void khdays_lower_standing_rows(int width, int height, int *row0, int *rows, uint32_t *layers_off);

void khdays_lower_free(KhdaysLowerLayout *layout);

#endif
