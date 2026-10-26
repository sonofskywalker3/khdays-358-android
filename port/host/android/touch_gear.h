/* The settings key a touch screen taps (Android): a gear in the picture's upper right corner, shown
 * while the game is played by touch -- no gamepad, or the screen touched since one was last used. A
 * phone has no Esc and no pad's settings button; the title's own key (a mod's `button open_settings`)
 * is only there with the HD menus. */
#ifndef KHDAYS_HOST_ANDROID_TOUCH_GEAR_H
#define KHDAYS_HOST_ANDROID_TOUCH_GEAR_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the gear over the frame when it is to be shown (in the window's pixels), and remembers where. */
void khdays_touch_gear_draw(SDL_Renderer *renderer);

/* Whether the gear drawn last is at a window point in pixels (a mouse event's coordinates times the
 * window's pixel density). */
int khdays_touch_gear_at(float x, float y);

#ifdef __cplusplus
}
#endif

#endif
