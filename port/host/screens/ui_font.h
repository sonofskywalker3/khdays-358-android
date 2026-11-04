/* Text for the port's own screens, in the game's own font (ui_font.cpp):
 * text/font_eu_10.nftr, found through the port's file system so a mod's font
 * replaces it. When it cannot be loaded, callers fall back to SDL's built-in
 * debug text. Host thread only. */
#ifndef KHDAYS_UI_FONT_H
#define KHDAYS_UI_FONT_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loads the font; 1 if it is ready. Logs why once if it is not. */
int khdays_ui_font_load(void);

/* The height of a line of text, in font pixels (before any scaling). */
int khdays_ui_font_line_height(void);

/* How wide `utf8` is, in font pixels, without drawing it; SDL's debug
 * letters (8 pixels each) when the font is not available. */
int khdays_ui_font_width(const char *utf8);

/* A texture holding `utf8` in white on transparent (tint it with
 * SDL_SetTextureColorMod), and its size; NULL when the font is not
 * available. The texture is cached and owned here. */
SDL_Texture *khdays_ui_font_text(SDL_Renderer *renderer, const char *utf8, int *width, int *height);

/* `utf8` as white ARGB pixels on clear, for what draws outside SDL; freed with SDL_free. NULL when the font is not
 * available. */
Uint32 *khdays_ui_font_pixels(const char *utf8, int *width, int *height);

/* Drops every cached texture (before their renderer goes away). */
void khdays_ui_font_forget(void);

#ifdef __cplusplus
}
#endif

#endif
