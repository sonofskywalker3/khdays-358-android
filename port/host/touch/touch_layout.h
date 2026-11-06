/* The on-screen controls' places (touch_layout.c), plain arithmetic for the tests: each control sized in
 * density-independent points, so it is as large under the thumb on a phone as on a tablet, and the HUD's groups
 * moved clear of them (ui/hud_layout.h). */
#ifndef KHDAYS_TOUCH_LAYOUT_H
#define KHDAYS_TOUCH_LAYOUT_H

#include "../../ui/hud_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    KHDAYS_TOUCH_STICK,
    KHDAYS_TOUCH_DPAD,
    KHDAYS_TOUCH_L,
    KHDAYS_TOUCH_R,
    KHDAYS_TOUCH_FACE,
    KHDAYS_TOUCH_LOCK,
    KHDAYS_TOUCH_SELECT,
    KHDAYS_TOUCH_START,
    KHDAYS_TOUCH_GEAR,
    KHDAYS_TOUCH_CONTROLS
};

/* The layouts the settings offer. */
enum { KHDAYS_TOUCH_LAYOUT_A, KHDAYS_TOUCH_LAYOUT_C, KHDAYS_TOUCH_LAYOUT_CUSTOM, KHDAYS_TOUCH_LAYOUTS };

/* A rectangle of the window, in its pixels. */
typedef struct KhdaysTouchBox {
    float x0, y0, x1, y1;
} KhdaysTouchBox;

/* The window and the picture in it. */
typedef struct KhdaysTouchScreen {
    float w, h;     /* the window's pixels */
    float density;  /* pixels a point (Android's dp) */
    float size;     /* the player's size for the controls, 1 the default */
    int columns;    /* the picture's width in DS dots */
    float hud;      /* the HUD's factor (khdays_composition_hud_factor) */
} KhdaysTouchScreen;

typedef struct KhdaysTouchLayout {
    KhdaysTouchBox box[KHDAYS_TOUCH_CONTROLS];
    KhdaysHudPlace hud[KHDAYS_HUD_GROUPS];
    float dots_x, dots_y; /* the picture's left and top in the window */
    float dot;            /* its pixels a DS dot */
} KhdaysTouchLayout;

/* The picture's place in the window, as the logical presentation letterboxes it. */
void khdays_touch_picture(const KhdaysTouchScreen *screen, float *x, float *y, float *dot);

/* Layout A or C on this screen. */
void khdays_touch_layout_make(int layout, const KhdaysTouchScreen *screen, KhdaysTouchLayout *out);

/* The player's own: `hud` the groups' places (NULL: the composition's), `control` each control's place in points
 * (layout_editor.h; one not set stays as layout C has it). */
void khdays_touch_layout_custom(const KhdaysTouchScreen *screen, const KhdaysHudPlace *hud, const KhdaysHudPlace *control,
                                KhdaysTouchLayout *out);

/* Whether a window's point is in a box grown by `reach` pixels on each side. */
int khdays_touch_in_box(const KhdaysTouchBox *box, float x, float y, float reach);

/* The centre of the face button (0 A, 1 B, 2 X, 3 Y) in the cluster's box, and its radius. */
void khdays_touch_face_button(const KhdaysTouchBox *cluster, int button, float *x, float *y, float *r);

#ifdef __cplusplus
}
#endif

#endif
