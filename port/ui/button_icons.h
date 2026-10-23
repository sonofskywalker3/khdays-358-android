/* The DS's buttons in the game's fonts, and the icons the HD fonts' text draws in their place
 * (button_icons.c): for each button, the input the player has for it on the device played. */
#ifndef KHDAYS_UI_BUTTON_ICONS_H
#define KHDAYS_UI_BUTTON_ICONS_H

#include <stdint.h>

enum {
    KHDAYS_BUTTON_NONE,
    KHDAYS_BUTTON_A,
    KHDAYS_BUTTON_B,
    KHDAYS_BUTTON_X,
    KHDAYS_BUTTON_Y,
    KHDAYS_BUTTON_L,
    KHDAYS_BUTTON_R,
    KHDAYS_BUTTON_START,
    KHDAYS_BUTTON_SELECT,
    KHDAYS_BUTTON_TARGET_NEXT, /* no glyph's: the lock-on tutorial's R that cycles the targets (text.c) */
    KHDAYS_BUTTON_UP,
    KHDAYS_BUTTON_DOWN,
    KHDAYS_BUTTON_LEFT,
    KHDAYS_BUTTON_RIGHT,
    KHDAYS_BUTTON_DPAD,            /* the D-pad whole */
    KHDAYS_BUTTON_DPAD_HORIZONTAL, /* its left and right */
    KHDAYS_BUTTON_DPAD_VERTICAL,   /* its up and down */
    KHDAYS_BUTTON_COUNT
};

/* The button a character of the game's fonts draws (Days's own, in the U+33xx squares), and which
 * of its glyphs it is in `piece` (START and SELECT take three, 0 the first); KHDAYS_BUTTON_NONE for
 * any other character. */
static inline int khdays_button_glyph(uint32_t code, int *piece)
{
    static const struct {
        uint16_t code;
        uint8_t button, piece;
    } glyphs[] = {
        {0x3349u, KHDAYS_BUTTON_A, 0},
        {0x3314u, KHDAYS_BUTTON_B, 0},
        {0x3322u, KHDAYS_BUTTON_X, 0},
        {0x334du, KHDAYS_BUTTON_Y, 0},
        {0x3318u, KHDAYS_BUTTON_L, 0},
        {0x3327u, KHDAYS_BUTTON_R, 0},
        {0x3336u, KHDAYS_BUTTON_START, 0},
        {0x3351u, KHDAYS_BUTTON_START, 1},
        {0x3357u, KHDAYS_BUTTON_START, 2},
        {0x330du, KHDAYS_BUTTON_SELECT, 0},
        {0x3326u, KHDAYS_BUTTON_SELECT, 1},
        {0x3323u, KHDAYS_BUTTON_SELECT, 2},
        {0x332bu, KHDAYS_BUTTON_UP, 0},
        {0x334au, KHDAYS_BUTTON_DOWN, 0},
        {0x337bu, KHDAYS_BUTTON_LEFT, 0},
        {0x333bu, KHDAYS_BUTTON_RIGHT, 0},
        {0x3303u, KHDAYS_BUTTON_DPAD, 0},
        {0x337cu, KHDAYS_BUTTON_DPAD_HORIZONTAL, 0},
        {0x337du, KHDAYS_BUTTON_DPAD_VERTICAL, 0},
    };
    for (unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
        if (glyphs[i].code == code) {
            *piece = glyphs[i].piece;
            return glyphs[i].button;
        }
    }
    *piece = 0;
    return KHDAYS_BUTTON_NONE;
}

/* An icon of `iw` x `ih` texels in a box of `bw` x `bh` DS pixels: as large as fits, its proportions
 * kept, centred. Gives its top left in the box and the DS pixels a texel takes. */
static inline void khdays_button_fit(double bw, double bh, int iw, int ih, double *x, double *y, double *texel)
{
    const double sx = bw / iw, sy = bh / ih;
    *texel = sx < sy ? sx : sy;
    *x = (bw - iw * *texel) / 2;
    *y = (bh - ih * *texel) / 2;
}

/* The icon of the input the player has for `button` on the device played (the HD PACK's, else
 * Kenney's), RGBA rows of `width` x `height`; NULL when there is none, and the game's glyph is drawn.
 * `field`: the text is a tutorial of the field's controls, so the DS's buttons read as the PC's
 * controls (the D-pad command menu, the stick, the separate lock-on). */
const uint8_t *khdays_button_icon(int button, int field, int *width, int *height);

/* Changes whenever the icons would: the device played, the gamepad, the bindings. */
uint32_t khdays_button_icons_signature(void);

#endif
