/* The on-screen controls on one screen (touch_overlay.c): shown over the picture on a touch screen without a
 * second display, turned into the DS's buttons with the rest of the input. With two screens, or a controller in
 * use, nothing of this runs: no cost, and the HUD where the composition has it. */
#ifndef KHDAYS_TOUCH_OVERLAY_H
#define KHDAYS_TOUCH_OVERLAY_H

#include <SDL3/SDL.h>

#include "../app/host.h"
#include "../app/profile.h"
#include "touch_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the controls over the frame in the window's pixels and keeps the HUD's layout up to date. Returns 1 when
 * they are shown (their gear stands for the corner's). */
int khdays_touch_draw(SDL_Renderer *renderer);

/* One SDL event, before the rest see it. Returns 1 when it was the controls'. */
int khdays_touch_event(const SDL_Event *event, SDL_Renderer *renderer);

/* What the controls' view said (Android; host/android/touch_bridge.h). Returns 1 when the event was that. */
int khdays_touch_view_event(const SDL_Event *event);

/* While the layout editor is open: a finger on a control resizes it, not moves it. Whether this build can: Android's
 * view. */
void khdays_touch_edit_resizing(int resizing);
int khdays_touch_edit_can_resize(void);

/* Whether the controls are on the screen now: a touch then never reaches the game as the DS's stylus. */
int khdays_touch_shown(void);

/* 1 once after the controls' gear was tapped. */
int khdays_touch_take_settings(void);

/* Lets go of everything (the settings menu opened, the app went away). */
void khdays_touch_release(void);

/* Whether this build and screen have the controls at all (the layout editor offers them then). */
int khdays_touch_available(void);

/* Pixels a point of this window. */
float khdays_touch_density(SDL_Renderer *renderer);

/* While the layout editor is open: the controls drawn at these places, not played; NULL ends that. */
void khdays_touch_edit(const KhdaysTouchLayout *layout);

/* The settings changed: the places made again at the next picture. */
void khdays_touch_relayout(void);

/* Adds what the fingers hold to a snapshot (devices.c). */
void khdays_touch_apply(const KhdaysProfile *profile, KhdaysInputSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
