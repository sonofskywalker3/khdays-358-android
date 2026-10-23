/* The game's own surface on Android (the main display's SurfaceView, MainActivity.FastSurface): a
 * frame is presented only while it is there. Between the surface going (Home, the device asleep) and
 * coming back -- after the activity resumes, a moment before its new surface exists -- SDL holds no
 * window to show into, and presenting then made its Vulkan swapchain rebuild itself on none (a crash
 * in CreateAndroidSurfaceKHR, the game started over). */
#ifndef KHDAYS_HOST_ANDROID_GAME_SURFACE_H
#define KHDAYS_HOST_ANDROID_GAME_SURFACE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Before a frame is drawn and presented: 1 with the surface held until khdays_game_surface_frame_end
 * (its going waits for the frame), or 0 when there is none now (the frame is not drawn). */
int khdays_game_surface_frame_begin(void);

/* After the frame was presented. */
void khdays_game_surface_frame_end(void);

#ifdef __cplusplus
}
#endif

#endif
