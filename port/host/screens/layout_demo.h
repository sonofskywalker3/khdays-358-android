/* Showing the layout editor without anyone at the mouse, for the tests and a short film of it (KHDAYS_LAYOUT_DEMO, the
 * steps split by `;`, each `<milliseconds to wait> <field|open|down x y|move x y|up x y|shot|quit>`; `field` waits for
 * play, `shot` saves the window into KHDAYS_LAYOUT_DEMO_DIR). */
#ifndef KHDAYS_LAYOUT_DEMO_H
#define KHDAYS_LAYOUT_DEMO_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void khdays_layout_demo_start(SDL_Window *window);

/* Returns 1 when the event was the script's. */
int khdays_layout_demo_event(const SDL_Event *event);

/* In the frame, before it is shown. */
void khdays_layout_demo_frame(SDL_Renderer *renderer);

#ifdef __cplusplus
}
#endif

#endif
