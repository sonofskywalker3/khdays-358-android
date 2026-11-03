/* touch_input.h. A finger's job is settled where it lands, except on the face buttons and the D-pad, where it
 * goes on with whatever is under it, so a thumb slides from one to the next without lifting. */
#include "touch_input.h"
#include "../app/profile.h"

#include <math.h>
#include <string.h>

enum {
    JOB_FREE,
    JOB_STICK,
    JOB_DPAD,
    JOB_FACE,
    JOB_L,
    JOB_R,
    JOB_LOCK,
    JOB_SELECT,
    JOB_START,
    JOB_GEAR,
    JOB_CAMERA,
    JOB_TAP,
    JOB_DECK
};

#define DPAD_DEAD 0.18f       /* of the D-pad's half width: no direction in its middle */
#define DPAD_SECTOR 22.5f     /* degrees: a diagonal this near gives two directions */
#define BOTH_WITHIN 1.35f     /* a second face button, when it is this much further than the nearest */
#define FACE_REACH 1.55f      /* of a button's radius: still that button */
#define REACH_OF_BOX 0.12f    /* the small controls' hit grown by this share of their height */
#define PI_F 3.14159265f

static const int face_bit[4] = {KHDAYS_ACT_A, KHDAYS_ACT_B, KHDAYS_ACT_X, KHDAYS_ACT_Y};

static float box_reach(const KhdaysTouchBox *b)
{
    return (b->y1 - b->y0) * REACH_OF_BOX;
}

static int in_control(const KhdaysTouchInput *t, int control, float x, float y)
{
    const KhdaysTouchBox *b = &t->layout.box[control];
    return khdays_touch_in_box(b, x, y, box_reach(b));
}

static int deck_row_at(const KhdaysTouchInput *t, float x, float y)
{
    for (int r = 0; r < KHDAYS_TOUCH_DECK_ROWS; ++r) {
        const KhdaysTouchBox *b = &t->deck_rows[r];
        if (b->x1 > b->x0 && khdays_touch_in_box(b, x, y, 0.0f)) {
            return r;
        }
    }
    return -1;
}

/* The job a finger takes landing here in this mode, JOB_FREE when no control is there. */
static int job_at(const KhdaysTouchInput *t, float x, float y)
{
    switch (t->mode) {
    case KHDAYS_TOUCH_MODE_TALK:
        return in_control(t, KHDAYS_TOUCH_START, x, y) ? JOB_START : JOB_TAP;
    case KHDAYS_TOUCH_MODE_MENU:
        if (in_control(t, KHDAYS_TOUCH_DPAD, x, y)) {
            return JOB_DPAD;
        }
        break;
    case KHDAYS_TOUCH_MODE_FIELD: break;
    default: return JOB_FREE;
    }
    if (in_control(t, KHDAYS_TOUCH_FACE, x, y)) {
        return JOB_FACE;
    }
    if (in_control(t, KHDAYS_TOUCH_L, x, y)) {
        return JOB_L;
    }
    if (in_control(t, KHDAYS_TOUCH_R, x, y)) {
        return JOB_R;
    }
    if (in_control(t, KHDAYS_TOUCH_SELECT, x, y)) {
        return JOB_SELECT;
    }
    if (in_control(t, KHDAYS_TOUCH_START, x, y)) {
        return JOB_START;
    }
    if (in_control(t, KHDAYS_TOUCH_GEAR, x, y)) {
        return JOB_GEAR;
    }
    if (t->mode == KHDAYS_TOUCH_MODE_MENU) {
        return JOB_FREE;
    }
    if (in_control(t, KHDAYS_TOUCH_LOCK, x, y)) {
        return JOB_LOCK;
    }
    if (deck_row_at(t, x, y) >= 0) {
        return JOB_DECK;
    }
    return x < t->stick_zone ? JOB_STICK : JOB_CAMERA;
}

/* The bits of the face button or two under a point. */
static uint16_t face_at(const KhdaysTouchInput *t, float x, float y)
{
    float d[4], radius = 1.0f;
    int nearest = 0;
    uint16_t bits = 0;
    for (int i = 0; i < 4; ++i) {
        float bx, by;
        khdays_touch_face_button(&t->layout.box[KHDAYS_TOUCH_FACE], i, &bx, &by, &radius);
        d[i] = hypotf(x - bx, y - by);
        nearest = d[i] < d[nearest] ? i : nearest;
    }
    if (d[nearest] > radius * FACE_REACH * 1.6f) {
        return 0; /* the box's empty middle is the nearest's too, its far corners are nobody's */
    }
    bits = (uint16_t)(1u << face_bit[nearest]);
    for (int i = 0; i < 4; ++i) {
        if (i != nearest && d[i] < d[nearest] * BOTH_WITHIN && d[i] < radius * FACE_REACH) {
            bits |= (uint16_t)(1u << face_bit[i]);
        }
    }
    return bits;
}

/* The direction bits of the D-pad under a point. */
static uint16_t dpad_at(const KhdaysTouchInput *t, float x, float y)
{
    const KhdaysTouchBox *b = &t->layout.box[KHDAYS_TOUCH_DPAD];
    const float half = (b->x1 - b->x0) / 2.0f;
    const float dx = x - (b->x0 + half), dy = y - (b->y0 + b->y1) / 2.0f;
    uint16_t bits = 0;
    float angle;
    if (hypotf(dx, dy) < half * DPAD_DEAD) {
        return 0;
    }
    angle = atan2f(-dy, dx) * 180.0f / PI_F; /* 0 right, 90 up */
    if (fabsf(angle) < 90.0f - DPAD_SECTOR) {
        bits |= 1u << KHDAYS_ACT_RIGHT;
    }
    if (fabsf(angle) > 90.0f + DPAD_SECTOR) {
        bits |= 1u << KHDAYS_ACT_LEFT;
    }
    if (angle > DPAD_SECTOR && angle < 180.0f - DPAD_SECTOR) {
        bits |= 1u << KHDAYS_ACT_UP;
    }
    if (angle < -DPAD_SECTOR && angle > -180.0f + DPAD_SECTOR) {
        bits |= 1u << KHDAYS_ACT_DOWN;
    }
    return bits;
}

/* Works out what is held from the fingers. */
static void held(KhdaysTouchInput *t)
{
    t->buttons = 0;
    t->lock_on = 0;
    t->stick_held = 0;
    t->stick_x = t->stick_y = 0.0f;
    for (int i = 0; i < KHDAYS_TOUCH_FINGERS; ++i) {
        const KhdaysTouchFinger *f = &t->finger[i];
        switch (f->job) {
        case JOB_STICK:
            t->stick_held = 1;
            t->stick_x = t->stick_radius > 0.0f ? (f->x - f->x0) / t->stick_radius : 0.0f;
            t->stick_y = t->stick_radius > 0.0f ? (f->y - f->y0) / t->stick_radius : 0.0f;
            break;
        case JOB_DPAD: t->buttons |= dpad_at(t, f->x, f->y); break;
        case JOB_FACE: t->buttons |= face_at(t, f->x, f->y); break;
        case JOB_L: t->buttons |= 1u << KHDAYS_ACT_L; break;
        case JOB_R: t->buttons |= 1u << KHDAYS_ACT_R; break;
        case JOB_LOCK: t->lock_on = 1; break;
        case JOB_SELECT: t->buttons |= 1u << KHDAYS_ACT_SELECT; break;
        case JOB_START: t->buttons |= 1u << KHDAYS_ACT_START; break;
        case JOB_TAP: {
            /* an event's answers (Yes, No) have no D-pad on the screen: a drag moves through them */
            const float dx = f->x - f->x0, dy = f->y - f->y0;
            if (hypotf(dx, dy) >= t->tap_travel) {
                t->buttons |= 1u << (fabsf(dx) > fabsf(dy) ? (dx > 0.0f ? KHDAYS_ACT_RIGHT : KHDAYS_ACT_LEFT)
                                                          : (dy > 0.0f ? KHDAYS_ACT_DOWN : KHDAYS_ACT_UP));
            }
            break;
        }
        default: break;
        }
    }
}

static KhdaysTouchFinger *finger_of(KhdaysTouchInput *t, uint64_t id)
{
    for (int i = 0; i < KHDAYS_TOUCH_FINGERS; ++i) {
        if (t->finger[i].job != JOB_FREE && t->finger[i].id == id) {
            return &t->finger[i];
        }
    }
    return NULL;
}

void khdays_touch_input_release(KhdaysTouchInput *t)
{
    memset(t->finger, 0, sizeof(t->finger));
    held(t);
}

void khdays_touch_input_mode(KhdaysTouchInput *t, int mode)
{
    if (mode != t->mode) {
        t->mode = mode;
        khdays_touch_input_release(t);
    }
}

int khdays_touch_input_wants(const KhdaysTouchInput *t, float x, float y)
{
    return job_at(t, x, y) != JOB_FREE;
}

void khdays_touch_input_down(KhdaysTouchInput *t, uint64_t id, float x, float y)
{
    const int job = job_at(t, x, y);
    KhdaysTouchFinger *f = finger_of(t, id);
    if (job == JOB_FREE) {
        return;
    }
    for (int i = 0; f == NULL && i < KHDAYS_TOUCH_FINGERS; ++i) {
        f = t->finger[i].job == JOB_FREE ? &t->finger[i] : NULL;
    }
    if (f == NULL) {
        return; /* more fingers than slots */
    }
    f->id = id;
    f->job = job;
    f->row = job == JOB_DECK ? deck_row_at(t, x, y) : -1;
    f->x0 = f->x = x;
    f->y0 = f->y = y;
    held(t);
}

void khdays_touch_input_move(KhdaysTouchInput *t, uint64_t id, float x, float y)
{
    KhdaysTouchFinger *f = finger_of(t, id);
    if (f == NULL) {
        return;
    }
    if (f->job == JOB_CAMERA) {
        t->camera_x += x - f->x;
        t->camera_y += y - f->y;
    }
    f->x = x;
    f->y = y;
    if (f->job == JOB_STICK && t->stick_radius > 0.0f) {
        /* the stick's middle follows a thumb gone past its edge, so turning back answers at once */
        const float dx = x - f->x0, dy = y - f->y0, far = hypotf(dx, dy);
        if (far > t->stick_radius) {
            const float over = (far - t->stick_radius) / far;
            f->x0 += dx * over;
            f->y0 += dy * over;
        }
    }
    held(t);
}

void khdays_touch_input_up(KhdaysTouchInput *t, uint64_t id)
{
    KhdaysTouchFinger *f = finger_of(t, id);
    if (f == NULL) {
        return;
    }
    if (f->job == JOB_DECK && deck_row_at(t, f->x, f->y) == f->row) {
        t->deck_row = (uint8_t)f->row;
        ++t->deck_serial;
    }
    if (f->job == JOB_GEAR && in_control(t, KHDAYS_TOUCH_GEAR, f->x, f->y)) {
        t->settings = 1;
    }
    {
        const int tap = f->job == JOB_TAP && hypotf(f->x - f->x0, f->y - f->y0) < t->tap_travel;
        f->job = JOB_FREE;
        held(t);
        if (tap) {
            t->tap_pulse = 1;
            t->buttons |= 1u << KHDAYS_ACT_A;
        }
    }
}

void khdays_touch_input_cancel(KhdaysTouchInput *t, uint64_t id)
{
    KhdaysTouchFinger *f = finger_of(t, id);
    if (f != NULL) {
        f->job = JOB_FREE;
        held(t);
    }
}

unsigned khdays_touch_input_pressed(const KhdaysTouchInput *t)
{
    static const signed char control_of[] = {
        [JOB_STICK] = KHDAYS_TOUCH_STICK, [JOB_DPAD] = KHDAYS_TOUCH_DPAD,     [JOB_FACE] = KHDAYS_TOUCH_FACE,
        [JOB_L] = KHDAYS_TOUCH_L,         [JOB_R] = KHDAYS_TOUCH_R,           [JOB_LOCK] = KHDAYS_TOUCH_LOCK,
        [JOB_SELECT] = KHDAYS_TOUCH_SELECT, [JOB_START] = KHDAYS_TOUCH_START, [JOB_GEAR] = KHDAYS_TOUCH_GEAR,
    };
    unsigned bits = 0;
    for (int i = 0; i < KHDAYS_TOUCH_FINGERS; ++i) {
        const int job = t->finger[i].job;
        if (job >= JOB_STICK && job <= JOB_GEAR) {
            bits |= 1u << control_of[job];
        }
    }
    return bits;
}
