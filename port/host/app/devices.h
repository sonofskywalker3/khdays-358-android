/* The keyboard, the mouse and the gamepads, as SDL reports them (devices.c).
 * It keeps what is held, picks the gamepad that plays, and after every change
 * turns it into an input snapshot (bindings.c) and hands that on. It runs on
 * the window's thread, the one that owns SDL. */
#ifndef KHDAYS_DEVICES_H
#define KHDAYS_DEVICES_H

#include "bindings.h"

#include <SDL3/SDL.h>

typedef void (*KhdaysPublish)(const KhdaysInputSnapshot *snapshot);

/* Starts with a profile (copied) and the function that receives every new
 * snapshot. SDL's gamepad subsystem must already be initialized. */
void khdays_devices_init(const KhdaysProfile *profile, KhdaysPublish publish);

/* A new profile (copied), for example from the settings menu. */
void khdays_devices_set_profile(const KhdaysProfile *profile);
const KhdaysProfile *khdays_devices_profile(void);

/* The on-screen controls (host/touch/touch_overlay.h), when the game has them: `apply` adds what they hold to each
 * snapshot, `relayout` hears of a new profile. Before khdays_devices_init. */
void khdays_devices_set_touch(void (*apply)(const KhdaysProfile *profile, KhdaysInputSnapshot *snapshot),
                              void (*relayout)(void));

/* The snapshot made again and handed on (the on-screen controls changed what they hold). */
void khdays_devices_resend(void);

/* One SDL event. `renderer` maps the mouse onto the DS screens. */
void khdays_devices_handle(const SDL_Event *event, SDL_Renderer *renderer);

/* Each time the window's loop wakes: a wheel's notch let go once its moment has passed. */
void khdays_devices_frame(void);

/* While off, the snapshots say `ui_open` and hold nothing, whatever is
 * pressed (the settings menu is using the input). When turned back on,
 * anything still held is ignored until it is released. */
void khdays_devices_set_game_input(int on);

/* Where a finger on the window is the DS's stylus: `map` says whether the window point (0..1 of the window's
 * width and height) shows the lower screen and which of its dots. The first finger down on the lower screen
 * is the stylus in the snapshots (touch_x, touch_y, touching) until it lifts or leaves the lower screen;
 * other fingers are not. NULL: no finger is the stylus (the default). */
void khdays_devices_set_stylus_map(int (*map)(float nx, float ny, int *ds_x, int *ds_y));

/* The DS's buttons (a bit per KHDAYS_ACT_*, as the snapshots hold them) pressed for one snapshot: the next
 * snapshot holds them and the one after lets go, which the game counts as one press, as it does a tap's. */
void khdays_devices_press_once(uint16_t ds_buttons);

/* 1 once after a gamepad input bound to `open_settings` was pressed. */
int khdays_devices_take_settings_request(void);

/* How many times the HUD mode's binding was pressed since the last call (the one-screen picture cycles
 * complete, exploration, none). */
int khdays_devices_take_hud_mode_request(void);

/* The active gamepad's name, or NULL when none is connected. */
const char *khdays_devices_gamepad_name(void);

/* Whether a gamepad names its face buttons by mark, the Nintendo way (AYN's built-in pads), though SDL does not
 * call it a Nintendo pad. */
int khdays_devices_pad_marked(SDL_Gamepad *gamepad);

/* The active gamepad itself (for its button labels), or NULL. */
SDL_Gamepad *khdays_devices_gamepad(void);

/* Whether the gamepad is what is played: its buttons or sticks used since the keyboard or the
 * mouse last were (for the prompts the picture shows). */
int khdays_devices_pad_in_use(void);

/* The screen was touched (a touch the game is not given): the pad is no longer what is used. */
void khdays_devices_screen_touched(void);

/* The SDL button behind one of our positional inputs, or
 * SDL_GAMEPAD_BUTTON_INVALID (the triggers are axes). */
SDL_GamepadButton khdays_devices_sdl_button(int input);

/* What is held right now (for the settings menu's displays). */
const KhdaysDeviceState *khdays_devices_state(void);

/* Messages for the player (a controller that was not usable), oldest first;
 * returns 0 when there are none left. */
int khdays_devices_take_notice(char *text, int size);

#endif
