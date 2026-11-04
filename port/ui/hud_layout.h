/* The field's HUD on one screen in groups of pieces that move together, each with the place the player's
 * layout gives it. A group's shapes (`group <name>` in compositions.txt, docs/COMPOSITIONS.md) keep what they
 * show and how they lie against each other; the group moves as one. No group moves until a layout says so. */
#ifndef KHDAYS_UI_HUD_LAYOUT_H
#define KHDAYS_UI_HUD_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    KHDAYS_HUD_MISSION,   /* the mission's panel, its mirrored edge, the INFORMATION banner */
    KHDAYS_HUD_COUNTER,   /* the hearts' counter's 3D and the notifications beside it */
    KHDAYS_HUD_MAP,       /* the map and its ground, the enemy's health, the sigils */
    KHDAYS_HUD_COUNTDOWN, /* the countdown and the lock-on */
    KHDAYS_HUD_DECK,      /* the command deck and the reaction command */
    KHDAYS_HUD_PARTY,     /* the player's health and the allies', two or four members alike */
    KHDAYS_HUD_GROUPS
};

/* The sides a place is measured from. */
enum { KHDAYS_HUD_AT_START, KHDAYS_HUD_AT_MIDDLE, KHDAYS_HUD_AT_END };

/* Where a group goes: the point its own corner of the picture would be at, measured in the picture's DS dots
 * from a side of the picture, so that it stays that far from that side in a picture of another shape. */
typedef struct KhdaysHudPlace {
    int set;    /* 0: where the composition has it */
    int side_x; /* KHDAYS_HUD_AT_*: left, middle, right */
    int side_y; /* top, bottom (never the middle) */
    float x, y; /* inwards from the side (from the middle: rightwards) */
    float w, h; /* an on-screen control's own size in the same units; 0: the layout's */
} KhdaysHudPlace;

/* A rectangle of the picture, in its DS dots. */
typedef struct KhdaysHudBox {
    float x0, y0, x1, y1;
} KhdaysHudBox;

/* -1 when no group has that name. */
int khdays_hud_group_named(const char *name);
const char *khdays_hud_group_name(int group);

/* The place that leaves a group where the composition has it. */
KhdaysHudPlace khdays_hud_place_home(int group);

/* The place that moves a group by (dx, dy) from there. */
KhdaysHudPlace khdays_hud_place_moved(int group, float dx, float dy);

/* The same point measured from the sides nearest to `box`, the group's box in a picture `columns` wide. */
KhdaysHudPlace khdays_hud_place_nearest(const KhdaysHudPlace *place, const KhdaysHudBox *box, int group, int columns);

/* The layout in force from the next picture: an array of KHDAYS_HUD_GROUPS, or NULL for none. Any thread. */
void khdays_hud_layout_set(const KhdaysHudPlace *places);

/* How far `group` moves in a picture `columns` wide. Any thread. */
void khdays_hud_layout_offset(int group, int columns, float *dx, float *dy);

/* The pictures' making reports its groups' shapes where the composition has them, before they move; `begin` and
 * `end` around a picture. */
void khdays_hud_layout_begin(void);
void khdays_hud_layout_note(int group, float x0, float y0, float x1, float y1, float scale);
void khdays_hud_layout_end(void);

/* A group's box where the layout puts it in a picture `columns` wide, as the last picture that showed it had it,
 * and the scale of its first shape. Returns 0 when no picture has shown it yet. Any thread. */
int khdays_hud_layout_box(int group, int columns, KhdaysHudBox *box, float *scale);

/* That box without the room its shapes keep blank (the reaction command's over the deck, the allies' over the
 * player's health): what the last picture drew of the group. Returns 0 when no picture has shown it yet. */
int khdays_hud_layout_visible(int group, int columns, KhdaysHudBox *box);

/* That box for a group no picture has shown yet: about where and how large the port's composition has it, at the
 * HUD's factor `hud`, for placing it all the same. */
void khdays_hud_layout_box_expected(int group, int columns, float hud, KhdaysHudBox *box);

#ifdef __cplusplus
}
#endif

#endif
