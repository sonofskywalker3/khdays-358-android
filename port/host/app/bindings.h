/* Turns what is held on the keyboard and the gamepad into what the game
 * should see (bindings.c). It makes no SDL calls, so the tests can drive it
 * directly. */
#ifndef KHDAYS_BINDINGS_H
#define KHDAYS_BINDINGS_H

#include "host.h"
#include "profile.h"

#define KHDAYS_KEY_COUNT 512 /* SDL_SCANCODE_COUNT */

typedef struct KhdaysDeviceState {
    uint8_t key[KHDAYS_KEY_COUNT]; /* keyboard scancodes held */
    uint8_t pad[KHDAYS_PAD_COUNT]; /* gamepad buttons held; the triggers come from trigger[] */
    int16_t trigger[2];            /* left, right: 0..32767 */
    int16_t stick[2][2];           /* [left, right][x, y] in SDL's range; y grows downwards */
    uint8_t mouse[KHDAYS_MOUSE_COUNT]; /* the mouse's buttons held, a wheel's notch for a moment */
    double mouse_x, mouse_y;           /* the mouse camera: the mouse's motion summed, right and up positive */

    /* Inputs held when a mask was set (the settings menu closed, or the window
     * got the focus back). They are ignored until released, so the press that
     * closed the menu never reaches the game. */
    uint8_t key_masked[KHDAYS_KEY_COUNT];
    uint8_t pad_masked[KHDAYS_PAD_COUNT];
    uint8_t mouse_masked[KHDAYS_MOUSE_COUNT];
    uint8_t stick_masked[2];

    /* What each stick was doing last time, for the hysteresis. */
    int stick_active[2]; /* past its dead zone */
    int sector[2];       /* the D-pad direction it gives, 0..7, or -1 */
} KhdaysDeviceState;

/* Nothing held, nothing masked, no memory. */
void khdays_bindings_reset(KhdaysDeviceState *state);

/* Everything held now is ignored until it is released. */
void khdays_bindings_mask_held(KhdaysDeviceState *state);

/* Fills `out` (all but `ui_open`) from the profile and what is held. */
void khdays_bindings_resolve(const KhdaysProfile *profile, KhdaysDeviceState *state, KhdaysInputSnapshot *out);

/* The D-pad bits one stick gives: its direction as the DS D-pad would give
 * it (0, one or two of the KHDAYS_KEY_* direction bits), with the dead zone
 * and hysteresis of `stick`. `*active` and `*sector` are the stick's memory
 * from the previous call (start with 0 and -1). */
uint16_t khdays_stick_dpad(int x, int y, const KhdaysStick *stick, int *active, int *sector);

#endif
