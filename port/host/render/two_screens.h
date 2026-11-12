/* Where the DS's two screens go in the two-screen picture (gpu_present.c): one above the other, as the DS has
 * them, or side by side ([video] two_screens, profile.h). In DS pixels: the picture's size and each screen's
 * rectangle in it. */
#ifndef KHDAYS_TWO_SCREENS_H
#define KHDAYS_TWO_SCREENS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysScreenPlace {
    float x, y, w, h;
} KhdaysScreenPlace;

typedef struct KhdaysTwoScreensLayout {
    int width, height;       /* the picture */
    KhdaysScreenPlace upper; /* the upper screen's place */
    KhdaysScreenPlace lower; /* the lower screen's */
} KhdaysTwoScreensLayout;

/* The picture for two screens: stacked, `columns` wide (the DS's 256, or wider for the aspect) and two
 * screens tall, each screen as wide as it was drawn (`upper_w`, `lower_w`) in the middle; side by side,
 * two DS screens wide and one tall, the upper on the left, each the DS's 256 columns (a screen drawn wider
 * shows its middle 256). */
KhdaysTwoScreensLayout khdays_two_screens_layout(int side_by_side, int columns, float upper_w, float lower_w);

/* The 3D on both screens: the game draws a screen's view at one VBlank and the other's at the next, and the
 * screen it is not drawing shows the window's own copy of that screen's last picture (gfx_render.c). Whether a
 * copy taken at VBlank `kept_at` shows at VBlank `now`: when it is of the package drawn just before this one
 * (`before`), or, while the game changes screens and takes its capture at `now` (`both`), at most
 * KHDAYS_KEPT_VBLANKS old -- a package the window did not draw, or a change of screens the game skipped, leaves
 * the copy of the picture before on the screen, not the DS's own small capture. */
#define KHDAYS_KEPT_VBLANKS 4
int khdays_two_screens_kept_shows(unsigned now, unsigned kept_at, unsigned before, int both);

#ifdef __cplusplus
}
#endif

#endif
