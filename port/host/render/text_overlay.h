/* The port's text drawn apart from the picture: where the 2D is composed much coarser than the window
 * shows it (a weaker card's 2x under a 5x window), the HD font's strokes are thinner than a texel of the
 * picture and come out grey. Its elements marked KES_OVERLAY are left out of the composition and drawn
 * here, from images made at the window's size, over the finished one-screen picture. What that costs:
 * they are over everything in the picture (another shape's opaque texels too), take only the fades this
 * file gives them (the screens' master brightness, their layer's blend, their shape's opacity and dim),
 * and are in no picture read back from the card (KHDAYS_SHOTS_HD). */
#ifndef KHDAYS_HOST_TEXT_OVERLAY_H
#define KHDAYS_HOST_TEXT_OVERLAY_H

#include "gfx_render.h"

#include <SDL3/SDL.h>

/* The scale the overlay's images are made at for a picture whose 2D is composed at `scale2d` and shown at
 * `shown` window pixels a DS dot, in situation `scene` with `states` (ui/situation.h); 0: no overlay (the
 * 2D is fine enough, the situation is not one of the overlay's, or KHDAYS_TEXT_OVERLAY=0). What the game
 * marks by it is only offered: khdays_text_overlay_take decides, for the picture each package is drawn in. */
int khdays_text_overlay_scale(int scale2d, float shown, int scene, unsigned states);

/* Before `package` is composed: decides which of its KES_OVERLAY elements (and of the KOF_OVERLAY shapes of
 * `one`, the one-screen picture about to be made from it) are drawn here, and notes where; the others'
 * marks are taken off, so the composition and the pass draw them as any other. `allowed` 0 (a picture
 * something reads back: a capture, the 2D check) or `one` NULL (not the one-screen picture): none. */
void khdays_text_overlay_take(KhdaysFramePackage *package, const KhdaysOneScreenDraw *one, int allowed);
/* No picture with an overlay is shown. */
void khdays_text_overlay_clear(void);
/* Draws what was noted, the picture's DS dot (0, 0) at (`x`, 0) of the renderer's logical size. */
void khdays_text_overlay_draw(SDL_Renderer *renderer, float x);
/* Before the renderer goes (its textures go with it). */
void khdays_text_overlay_close(void);

/* Tests: the pieces noted last, each its image, its source in texels (x, y, w, h), its place in the
 * picture's DS dots (x, y, w, h), its opacity and how much of its colours its shape's `dim` leaves; the
 * topmost last. */
typedef struct KhdaysTextPiece {
    uint32_t image;
    float src[4], dst[4];
    float alpha, shade;
} KhdaysTextPiece;
const KhdaysTextPiece *khdays_text_overlay_pieces(int *count);

#endif
