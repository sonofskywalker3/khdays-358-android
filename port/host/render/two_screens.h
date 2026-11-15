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
 * (`before`), or, while the game changes screens VBlank by VBlank (`both`: the package's own flag, armed capture
 * or not), at most
 * KHDAYS_KEPT_VBLANKS old -- a package the window did not draw, or a change of screens the game skipped, leaves
 * the copy of the picture before on the screen, not the DS's own small capture. While the game still captures
 * (`armed`: a package drawn in the last KHDAYS_KEPT_VBLANKS had a capture armed; a game frame over two VBlanks
 * arms it on the first only; tearing the scene down stops it) up to KHDAYS_KEPT_VBLANKS_ARMED: a window drawing
 * two packages in four that misses one leaves the copy 5 or 6 old (the Coliseum's scene on the Thor at 5x: one
 * package in eight to twenty fell back to the DS's capture with 4 alone). */
#define KHDAYS_KEPT_VBLANKS 4
#define KHDAYS_KEPT_VBLANKS_ARMED 8
int khdays_two_screens_kept_shows(unsigned now, unsigned kept_at, unsigned before, int both, int armed);

/* `armed` above, from the packages drawn: khdays_two_screens_armed_seen with each drawn package's VBlank and whether
 * it has a capture armed answers whether one had in the last KHDAYS_KEPT_VBLANKS. A VBlank before the last armed
 * one (a load, a replay) forgets it. Zeroed: none seen. */
typedef struct KhdaysArmedSeen {
    unsigned at;
    int valid;
} KhdaysArmedSeen;
int khdays_two_screens_armed_seen(KhdaysArmedSeen *seen, unsigned now, int armed_now);

/* A screen's 2D scale ([video] lower_resolution, profile.h): `setting` as chosen, or for 0
 * (auto, KHDAYS_RESOLUTION_WINDOW) `auto_scale`; never below `at_least` (the 3D's scale for the 3D's engine, else
 * 1). The same whether one screen or two is shown. */
int khdays_two_screens_scale2d(int setting, int auto_scale, int at_least);

#ifdef __cplusplus
}
#endif

#endif
