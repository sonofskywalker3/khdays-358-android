/* The on-screen controls' view (android/.../touch/TouchOverlayView.kt) and the game: what it is to show, and what
 * the fingers on it do, which reach the host thread as SDL events. */
#ifndef KHDAYS_HOST_ANDROID_TOUCH_BRIDGE_H
#define KHDAYS_HOST_ANDROID_TOUCH_BRIDGE_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The controls' boxes (KHDAYS_TOUCH_CONTROLS) and the deck's rows, four numbers each, in the window's pixels. */
#define KHDAYS_TOUCH_SHOWN_FLOATS ((9 + 3) * 4)

typedef struct KhdaysTouchShown {
    int mode;      /* KHDAYS_TOUCH_MODE_* */
    int edit;      /* 0 play, 1 moving the controls, 2 resizing them */
    int style;     /* the art's, from 0 */
    int opacity;   /* percent */
    int vibration;
    float w, h;    /* the window's pixels */
    float boxes[KHDAYS_TOUCH_SHOWN_FLOATS];
} KhdaysTouchShown;

enum { KHDAYS_TOUCH_BRIDGE_NONE, KHDAYS_TOUCH_BRIDGE_HELD, KHDAYS_TOUCH_BRIDGE_GEAR, KHDAYS_TOUCH_BRIDGE_BOX };

typedef struct KhdaysTouchBridgeEvent {
    int kind;
    Uint16 bits;            /* HELD: the DS's buttons, a bit per KHDAYS_ACT_* */
    int stick_held, lock_on;
    float stick_x, stick_y; /* -1..1, y growing downwards */
    int control;            /* BOX: the control moved, KHDAYS_TOUCH_* */
    float x0, y0, x1, y1;   /* where it is now, in the window's pixels */
} KhdaysTouchBridgeEvent;

/* Whether an SDL event is the view's: read into `out`, and what it carried freed. */
int khdays_touch_bridge_take(const SDL_Event *event, KhdaysTouchBridgeEvent *out);

/* Tells the view what to show. Host thread. */
void khdays_touch_bridge_show(const KhdaysTouchShown *shown);

/* The words on Select and Start: `height` rows of opaque-or-clear white pixels (ARGB). Host thread. */
void khdays_touch_bridge_labels(const Uint32 *select, int select_width, const Uint32 *start, int start_width, int height);

#ifdef __cplusplus
}
#endif

#endif
