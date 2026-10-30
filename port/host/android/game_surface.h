/* The game's own surface on Android (the main display's SurfaceView, MainActivity.FastSurface): a
 * frame is presented only while it is there. Between the surface going (Home, the device asleep) and
 * coming back -- after the activity resumes, a moment before its new surface exists -- SDL holds no
 * window to show into, and presenting then made its Vulkan swapchain rebuild itself on none (a crash
 * in CreateAndroidSurfaceKHR, the game started over). When it comes back the swapchain is made again on
 * it (gpu_present.h): SDL's Vulkan swapchain stays on the old surface unless the driver reports that
 * surface lost, and a Mali phone showed only black after another app (beta 8). */
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

/* The surface came again after going (the player turned to another app and back): no frame is drawn
 * until the swapchain is made again on it, never on SDL's swapchain of the surface that went. Returns 1
 * with the surface held until khdays_game_surface_frame_end (it cannot go while the swapchain is made),
 * once each time it came again; 0 when it did not, or is not there now. */
int khdays_game_surface_renew_begin(void);

/* The swapchain could not be made yet: khdays_game_surface_renew_begin says so again (no frame drawn). */
void khdays_game_surface_renew_again(void);

#ifdef __cplusplus
}
#endif

#endif
