/* The words shown while the graphics card's programs are made the first time ("Compiling shaders - first
 * boot only"): on Vulkan with no pipeline cache yet, the AYN Odin's screen stayed black for up to four minutes. */
#ifndef KHDAYS_HOST_FIRST_START_NOTICE_H
#define KHDAYS_HOST_FIRST_START_NOTICE_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the two lines, centred on black, and presents them (they stay until the next picture). */
void khdays_first_start_notice(SDL_Renderer *r);

#ifdef __cplusplus
}
#endif

#endif
