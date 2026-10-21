/* The port's settings menu (settings_menu.cpp): the whole window, the game dimmed behind it, sections
 * on the left and settings on the right, for a keyboard, a gamepad or a mouse alone. Drawn with Dear
 * ImGui, reading the input itself. While it is open the DS clock stops; a game loop that spins without
 * waiting (the opening movie's) keeps one core busy meanwhile. */
#ifndef KHDAYS_SETTINGS_UI_H
#define KHDAYS_SETTINGS_UI_H

#include "../app/profile.h"

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the menu on a working copy of `current`. */
void khdays_settings_open(const KhdaysProfile *current);

/* Opens the menu for the notice of a newer release alone: this build's version (`have`) and the new
 * release's tag (`latest`), and its three answers. The answer closes the menu (khdays_settings_handle
 * returns 1); khdays_settings_take_update_answer then gives it (KHDAYS_UPDATE_*, host/app/updates.h),
 * once; -1 when there is none. */
void khdays_settings_offer_update(const KhdaysProfile *current, const char *have, const char *latest);
int khdays_settings_take_update_answer(void);

int khdays_settings_is_open(void);

/* Handles one event while the menu is open. Returns 1 when the menu has just
 * closed: khdays_settings_changed() then says whether the profile changed
 * since it opened, and khdays_settings_profile() gives it. */
int khdays_settings_handle(const SDL_Event *event, SDL_Window *window);

/* Lets time pass (for repeating a held direction, and for the time limit
 * when waiting for a key or button). */
void khdays_settings_tick(void);

int khdays_settings_changed(void);
const KhdaysProfile *khdays_settings_profile(void);

/* While the menu is open: whether its profile differs from the one it opened
 * with (so it can be saved if the port is closed with the menu open). */
int khdays_settings_profile_changed(void);

/* Which parts of the working profile changed since the last call
 * (KHDAYS_CHANGE_* in settings_model.h), so they can be applied at once. */
unsigned khdays_settings_take_changes(void);

/* Draws the menu over whatever was rendered, in the renderer's own pixels. */
void khdays_settings_draw(SDL_Renderer *renderer, SDL_Window *window);

/* The menu as it opens, drawn while it is closed (the settings shown on a second display before it is
 * touched; the first touch opens it there). */
void khdays_settings_draw_closed(SDL_Renderer *renderer, const KhdaysProfile *current);

/* The renderer the menu drew with is about to be destroyed (a lost device,
 * another graphics API, the window closing): free its textures first. The
 * next draw starts again on whatever renderer it is given. */
void khdays_settings_renderer_gone(void);

/* What the rows depend on besides the profile: whether a multiplayer session
 * is open (then the game keeps running behind the menu) and
 * whether the HD videos are installed. Set when the menu opens. */
void khdays_settings_set_situation(int in_session, int hd_videos);

/* Whether this copy is a guest in a multiplayer session (its gameplay mods' options are shown locked:
 * the host's count). */
void khdays_settings_set_guest(int guest);

/* A line for the notice area (a problem in the settings file, a controller
 * that could not be used), shown while the menu is open (settings_corner.c). */
void khdays_settings_add_notice(const char *text);
int khdays_settings_notice_count(void);
const char *khdays_settings_notice(int n);

/* The FPS display (Video: Show FPS), in the window's top-left corner:
 * `pictures` drawn per second and the game's own `frames` per second. */
void khdays_settings_fps_draw(SDL_Renderer *renderer, int pictures, int frames);

/* The ping display ([network] show_ping): `line` in the top-left corner,
 * below the FPS display when `under_fps`. */
void khdays_settings_ping_draw(SDL_Renderer *renderer, const char *line, int under_fps);

/* A line of the mods' notices in the top-left corner, at the row given (0 the first, under the FPS
 * and ping lines when they show). */
void khdays_settings_corner_draw(SDL_Renderer *renderer, const char *line, int row);

/* Whether the picture is drawn by the graphics card. Without it, the Video
 * settings that need it are shown as unavailable. */
void khdays_settings_set_graphics_card(int available);

/* For the tests: where the last drawing put something, in the renderer's
 * pixels. It can be a section's entry (index 0 to KHDAYS_SECTION_COUNT-1, or
 * the count for Close), a row of the chosen section, a cell of a binding row
 * (index row * 4 + cell), a prompt's answer (0 or 1), or a row's value.
 * Returns 0 if it was not drawn (or is scrolled out of sight). */
enum { KHDAYS_PLACE_ENTRY, KHDAYS_PLACE_ROW, KHDAYS_PLACE_CELL, KHDAYS_PLACE_ANSWER, KHDAYS_PLACE_VALUE };
int khdays_settings_place(int what, int index, SDL_FRect *out);

/* For the tests: the chosen section, whether the focus is on its rows (or on
 * the column), the chosen row and cell; and a row's setting (KhdaysSettingId)
 * and, for a binding, its action. khdays_settings_row_info returns 0 past the
 * last row. */
void khdays_settings_focus(int *section, int *in_rows, int *row, int *cell);
int khdays_settings_row_info(int row, int *id, int *action);

/* For the tests: the gamepad family the Controls section shows (KhdaysPadFamily). */
int khdays_settings_pad_family(void);

/* One of the gamepads' icons the executable holds (Kenney's Input Prompts, by
 * its name: "switch_button_a"), as PNG bytes; NULL when there is none. For
 * the save list's Switch icons (save_screen.c). */
const unsigned char *khdays_pad_icon_png(const char *name, unsigned *size);

#ifdef __cplusplus
}
#endif

#endif
