/* What the fingers on the on-screen controls hold (touch_input.c), plain logic for the tests: any number of
 * fingers at once, each given a job where it lands; a thumb can slide between the face buttons. */
#ifndef KHDAYS_TOUCH_INPUT_H
#define KHDAYS_TOUCH_INPUT_H

#include "touch_layout.h"
#include "touch_needs.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_TOUCH_FINGERS 10
#define KHDAYS_TOUCH_DECK_ROWS 3

typedef struct KhdaysTouchFinger {
    uint64_t id;
    int job;      /* JOB_* in touch_input.c, 0 for a free slot */
    int row;      /* the deck's row it landed on */
    float x0, y0; /* the stick's middle, or where it was last (the camera) */
    float x, y;
} KhdaysTouchFinger;

typedef struct KhdaysTouchInput {
    KhdaysTouchLayout layout;
    int mode;
    int view_has_controls; /* a view of the system's own answers on the controls: what reaches here is the rest */
    float stick_radius;
    float stick_zone;                                  /* the stick lands left of this, the camera right of it */
    float tap_travel;                                  /* a movie's tap dragged further is no tap */
    KhdaysTouchBox deck_rows[KHDAYS_TOUCH_DECK_ROWS];  /* where the deck's rows are; empty when not shown */
    KhdaysTouchFinger finger[KHDAYS_TOUCH_FINGERS];

    uint16_t buttons;    /* the DS's buttons held, a bit per KHDAYS_ACT_* */
    int lock_on;         /* the lock-on's button held */
    float stick_x, stick_y; /* -1..1, y growing downwards; 0 when no finger holds it */
    int stick_held;
    double camera_x, camera_y; /* the drags' motion summed, in pixels */
    uint8_t deck_row;    /* the row tapped last */
    uint8_t deck_serial; /* counts the taps */
    int settings;        /* the gear was tapped; the caller clears it */
    int tap_pulse;       /* a movie's tap just ended: A held for this one snapshot; the caller clears it */
} KhdaysTouchInput;

/* Nothing held. The layout, the zone and the rows stay. */
void khdays_touch_input_release(KhdaysTouchInput *t);

/* A mode other than the one before lets go of everything held. */
void khdays_touch_input_mode(KhdaysTouchInput *t, int mode);

/* Whether a finger landing here would take a control of `mode` (not the picture beneath). */
int khdays_touch_input_wants(const KhdaysTouchInput *t, float x, float y);

void khdays_touch_input_down(KhdaysTouchInput *t, uint64_t id, float x, float y);
void khdays_touch_input_move(KhdaysTouchInput *t, uint64_t id, float x, float y);
void khdays_touch_input_up(KhdaysTouchInput *t, uint64_t id);

/* The system took the finger back: let go, with nothing a release would do. */
void khdays_touch_input_cancel(KhdaysTouchInput *t, uint64_t id);

/* The control a job holds, for showing it pressed: a bit per KHDAYS_TOUCH_* control held. */
unsigned khdays_touch_input_pressed(const KhdaysTouchInput *t);

#ifdef __cplusplus
}
#endif

#endif
