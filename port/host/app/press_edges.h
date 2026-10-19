/* Button presses and releases between two reads of the pad. The game reads it 30 times a second in
 * the field, so a quick tap can fall between two reads: here every press is read down at least once,
 * and a release followed by a new press is read up once, then down. Used for the player's buttons
 * (host/app/window.c) and the input scripts' (runtime/input/input.c). */
#ifndef KHDAYS_PRESS_EDGES_H
#define KHDAYS_PRESS_EDGES_H

#include <stdint.h>

typedef struct KhdaysPressEdges {
    uint16_t reported; /* what the last read was given */
    uint16_t pressed;  /* since then: pressed */
    uint16_t released; /* since then: let go */
    uint16_t again;    /* since then: pressed after being let go */
    uint16_t pending;  /* down at the next read: let go and pressed again before the last one */
} KhdaysPressEdges;

/* Records that the held buttons went from `before` to `now`. */
static inline void khdays_press_edges_note(KhdaysPressEdges *e, uint16_t before, uint16_t now)
{
    const uint16_t down = (uint16_t)(now & ~before), up = (uint16_t)(before & ~now);
    e->again |= (uint16_t)(down & e->released);
    e->pressed |= down;
    e->released |= up;
}

/* The buttons a read gets, given those held `now`. */
static inline uint16_t khdays_press_edges_take(KhdaysPressEdges *e, uint16_t now)
{
    const uint16_t up_now = (uint16_t)(e->reported & e->released);
    const uint16_t out = (uint16_t)((now | e->pending | (~e->reported & e->pressed)) & ~up_now);
    e->pending = (uint16_t)(up_now & e->again);
    e->reported = out;
    e->pressed = e->released = e->again = 0;
    return out;
}

#endif
