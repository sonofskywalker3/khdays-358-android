/* The lower DS screen on a device's second display (the AYN Thor's lower panel). The app's Java side
 * (android/.../BottomScreenHost.kt) puts a surface on the secondary display and hands it here with the
 * touches on it (bottom_screen.c's JNI); the presenter draws the lower screen into it each frame and
 * the input reads its touch. Without a second display nothing is attached and the window shows both
 * screens as on any system. */
#ifndef KHDAYS_HOST_ANDROID_BOTTOM_SCREEN_H
#define KHDAYS_HOST_ANDROID_BOTTOM_SCREEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether a surface on the second display takes the lower screen now. */
int khdays_bottom_attached(void);

/* Shows `rgba` (width x height pixels, R G B A bytes, `pitch` bytes a row) on the second display, the
 * largest 4:3 rectangle that fits, centred, nearest sampled. Does nothing when none is attached. */
void khdays_bottom_present(const uint8_t *rgba, int width, int height, int pitch);

/* The latest touch on the second display in DS coordinates (0-255, 0-191): 1 while it is down and the
 * touches are the game's (khdays_bottom_set_game_touch). */
int khdays_bottom_touch(int *x, int *y);

/* The second display's surface in pixels: 0 when none is attached. */
int khdays_bottom_size(int *w, int *h);

/* Shows `rgba` over the whole of the second display (a picture made at its size: the settings menu). */
void khdays_bottom_present_whole(const uint8_t *rgba, int width, int height, int pitch);

/* Whether the touches on the second display are the game's (1, the default), or nobody's while the
 * settings menu shows there (0). */
void khdays_bottom_set_game_touch(int on);

#ifdef __cplusplus
}
#endif

#endif
