/* Dragging the pieces of a layout about the window (layout_editor.c): plain arithmetic for the tests, knowing
 * nothing of what the pieces are or how they are drawn. The screen that shows it: host/screens/layout_editor_screen.h. */
#ifndef KHDAYS_UI_LAYOUT_EDITOR_H
#define KHDAYS_UI_LAYOUT_EDITOR_H

#include "hud_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_EDITOR_ITEMS 16

typedef struct KhdaysEditorItem {
    int kind;         /* the caller's: what it is */
    int id;           /* ... and which */
    KhdaysHudBox box; /* where it is, in the window's pixels */
    KhdaysHudBox room; /* how far past each side of it the area must still reach: what it sometimes draws there */
    KhdaysHudBox only; /* a part of the area it must stay in as well (the picture, for what the picture draws); empty: all */
} KhdaysEditorItem;

typedef struct KhdaysLayoutEditor {
    KhdaysEditorItem item[KHDAYS_EDITOR_ITEMS];
    int count;
    KhdaysHudBox area; /* the safe part of the window: an item stays wholly inside */
    float grid;        /* pixels between the snap's lines */
    int snap;
    int held;          /* the item being dragged, -1 none */
    float grab_x, grab_y;
} KhdaysLayoutEditor;

/* The settings menu asks for the screen; the window opens it. */
void khdays_editor_request(void);
int khdays_editor_take_request(void);

/* The item at a point, the smallest of those there (one inside another stays reachable), or -1. */
int khdays_editor_item_at(const KhdaysLayoutEditor *e, float x, float y);

/* A press: takes the item there; returns it, or -1. */
int khdays_editor_press(KhdaysLayoutEditor *e, float x, float y);

/* A motion with an item held: moves it under the point, inside the area, stopping at the others of its kind.
 * Returns 1 when it moved. */
int khdays_editor_drag(KhdaysLayoutEditor *e, float x, float y);

/* The release: the item held settles on the snap's lines. Returns 1 when that moved it. */
int khdays_editor_release(KhdaysLayoutEditor *e);

/* A piece lifted from two pictures of `w` x `h` RGBA pixels, one with it and one without: in `with`, what differs keeps
 * its colour and is opaque, the rest is clear. So it can be drawn moving over the second without making pictures. */
void khdays_editor_cutout(unsigned char *with, int with_pitch, const unsigned char *without, int without_pitch, int w, int h);

/* A group's place with its box moved from (x0, y0) to where it is now, both in the picture's dots: the point moved
 * with it, measured from the sides nearest its new box. */
KhdaysHudPlace khdays_editor_place(const KhdaysHudPlace *now, const KhdaysHudBox *from, const KhdaysHudBox *to, int group,
                                   int columns);

/* A window point's place from the window's nearest sides in the units `unit` pixels make, and back. */
KhdaysHudPlace khdays_editor_control_place(const KhdaysHudBox *box, float w, float h, float unit);
void khdays_editor_control_box(const KhdaysHudPlace *place, float bw, float bh, float w, float h, float unit,
                               KhdaysHudBox *out);

#ifdef __cplusplus
}
#endif

#endif
