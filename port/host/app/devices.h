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

/* One SDL event. `renderer` maps the mouse onto the DS screens. */
void khdays_devices_handle(const SDL_Event *event, SDL_Renderer *renderer);

/* Each time the window's loop wakes: a wheel's notch let go once its moment has passed. */
void khdays_devices_frame(void);

/* While off, the snapshots say `ui_open` and hold nothing, whatever is
 * pressed (the settings menu is using the input). When turned back on,
 * anything still held is ignored until it is released. */
void khdays_devices_set_game_input(int on);

/* 1 once after a gamepad input bound to `open_settings` was pressed. */
int khdays_devices_take_settings_request(void);

/* How many times the HUD mode's binding was pressed since the last call (the one-screen picture cycles
 * complete, exploration, none). */
int khdays_devices_take_hud_mode_request(void);

/* The active gamepad's name, or NULL when none is connected. */
const char *khdays_devices_gamepad_name(void);

/* The active gamepad itself (for its button labels), or NULL. */
SDL_Gamepad *khdays_devices_gamepad(void);

/* Whether the gamepad is what is played: its buttons or sticks used since the keyboard or the
 * mouse last were (for the prompts the picture shows). */
int khdays_devices_pad_in_use(void);

/* The SDL button behind one of our positional inputs, or
 * SDL_GAMEPAD_BUTTON_INVALID (the triggers are axes). */
SDL_GamepadButton khdays_devices_sdl_button(int input);

/* What is held right now (for the settings menu's displays). */
const KhdaysDeviceState *khdays_devices_state(void);

/* Messages for the player (a controller that was not usable), oldest first;
 * returns 0 when there are none left. */
int khdays_devices_take_notice(char *text, int size);

#endif
