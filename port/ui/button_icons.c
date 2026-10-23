/* The icons of the player's inputs drawn for the DS's buttons in the HD fonts' text (button_icons.h).
 * The collection's come from a mod's layouts (HD PACK's, made by tools/hd from the theater's
 * Icon.tm2: "icon xbox:east u v w h" in its atlas), cut out once; Kenney's from the executable
 * (the settings menu's). */
#include "button_icons.h"

#include "../host/app/devices.h"
#include "../host/app/profile.h"
#include "../host/render/png_io.h"
#include "../host/settings/settings_ui.h"
#include "../runtime/core/rom.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Icon {
    char name[64]; /* "xbox:east", "key:40" (a layout's), "kenney:xbox_dpad_none" */
    uint8_t *rgba; /* NULL: looked for, not there */
    int width, height;
} Icon;

static Icon *icons;
static int icon_count, icon_room;
static int layouts_read;

static Icon *find_icon(const char *name)
{
    for (int i = 0; i < icon_count; ++i) {
        if (strcmp(icons[i].name, name) == 0) {
            return &icons[i];
        }
    }
    return NULL;
}

static Icon *add_icon(const char *name, uint8_t *rgba, int width, int height)
{
    if (icon_count == icon_room) {
        const int room = icon_room ? icon_room * 2 : 64;
        Icon *grown = (Icon *)realloc(icons, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            free(rgba);
            return NULL;
        }
        icons = grown;
        icon_room = room;
    }
    Icon *icon = &icons[icon_count++];
    snprintf(icon->name, sizeof(icon->name), "%s", name);
    icon->rgba = rgba;
    icon->width = width;
    icon->height = height;
    return icon;
}

/* The icons of the layout at `path` (its "icon" lines, cut from the atlas its "atlas" line names
 * before them); 0 when it has none. */
static int read_layout(const char *path)
{
    char *text = (char *)SDL_LoadFile(path, NULL);
    const char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
    const int dir = back != NULL && (slash == NULL || back > slash) ? (int)(back - path + 1)
                    : slash != NULL                                 ? (int)(slash - path + 1)
                                                                    : 0;
    char atlas_path[1024] = "";
    uint8_t *atlas = NULL;
    int aw = 0, ah = 0, found = 0;
    for (char *line = text; line != NULL && *line != '\0';) {
        char *next = strpbrk(line, "\r\n");
        char name[64];
        int u, v, w, h;
        if (next != NULL) {
            *next++ = '\0';
        }
        if (strncmp(line, "atlas ", 6) == 0) {
            snprintf(atlas_path, sizeof(atlas_path), "%.*s%s", dir, path, line + 6);
        } else if (sscanf(line, "icon %63s %d %d %d %d", name, &u, &v, &w, &h) == 5) {
            if (atlas == NULL && atlas_path[0] != '\0') {
                atlas = khdays_png_read(atlas_path, &aw, &ah, NULL);
                atlas_path[0] = '\0'; /* read once */
            }
            uint8_t *rgba = atlas != NULL && w > 0 && h > 0 && u >= 0 && v >= 0 && u + w <= aw && v + h <= ah &&
                                    find_icon(name) == NULL
                                ? (uint8_t *)malloc((size_t)w * h * 4)
                                : NULL;
            if (rgba != NULL) {
                for (int y = 0; y < h; ++y) {
                    memcpy(rgba + (size_t)y * w * 4, atlas + ((size_t)(v + y) * aw + u) * 4, (size_t)w * 4);
                }
                found += add_icon(name, rgba, w, h) != NULL;
            }
        }
        line = next;
    }
    khdays_png_free(atlas);
    SDL_free(text);
    return found != 0;
}

/* The collection's icons from the first mod layout that has them (the camp's has the keys'). */
static void read_layouts(void)
{
    static const char *const files[] = {"camp/%s/camp.txt", "title/%s/title.txt", "save/%s/save.txt",
                                        "select/%s/select.txt"};
    static const char *const languages[] = {"en", "es", "fr", "de", "it"};
    layouts_read = 1;
    for (size_t f = 0; f < sizeof(files) / sizeof(files[0]); ++f) {
        for (size_t l = 0; l < sizeof(languages) / sizeof(languages[0]); ++l) {
            char relative[96], name[64], path[1024];
            snprintf(name, sizeof(name), files[f], languages[l]);
            snprintf(relative, sizeof(relative), "layouts/%s", name);
            if (khdays_mods_find(relative, path, sizeof(path)) && read_layout(path)) {
                return;
            }
        }
    }
}

/* One of Kenney's icons the executable holds, by its name. */
static const Icon *kenney_icon(const char *name)
{
    char key[64];
    snprintf(key, sizeof(key), "kenney:%s", name);
    const Icon *icon = find_icon(key);
    if (icon == NULL) {
        unsigned size = 0;
        const unsigned char *png = khdays_pad_icon_png(name, &size);
        int w = 0, h = 0;
        uint8_t *rgba = png != NULL ? khdays_png_read_memory(png, (int)size, &w, &h, NULL) : NULL;
        icon = add_icon(key, rgba, w, h);
    }
    return icon != NULL && icon->rgba != NULL ? icon : NULL;
}

/* The gamepad's family as the icons name it. */
static const char *family(SDL_Gamepad *pad)
{
    switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return "xbox";
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return "playstation";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    case SDL_GAMEPAD_TYPE_GAMECUBE: return "switch";
    default: return NULL;
    }
}

/* The icon of one input: a gamepad's (the collection's for an Xbox or a PlayStation one, else
 * Kenney's as the Controls section shows it), a key's (the collection's) or the mouse's (Kenney's). */
static const Icon *input_icon(SDL_Gamepad *pad, int input)
{
    static const char *const mouse[KHDAYS_MOUSE_COUNT] = {
        [KHDAYS_MOUSE_LEFT] = "mouse_left",
        [KHDAYS_MOUSE_MIDDLE] = "mouse_scroll",
        [KHDAYS_MOUSE_RIGHT] = "mouse_right",
        [KHDAYS_MOUSE_X1] = "mouse_side_back",
        [KHDAYS_MOUSE_X2] = "mouse_side_forward",
        [KHDAYS_MOUSE_WHEEL_UP] = "mouse_scroll_up",
        [KHDAYS_MOUSE_WHEEL_DOWN] = "mouse_scroll_down",
    };
    char name[64];
    if (pad != NULL) {
        const char *f = family(pad), *input_name = khdays_pad_input_name(input);
        if (f != NULL && strcmp(f, "switch") != 0 && input_name != NULL) {
            snprintf(name, sizeof(name), "%s:%s", f, input_name);
            const Icon *icon = find_icon(name);
            if (icon != NULL) {
                return icon;
            }
        }
        const char *kenney = khdays_pad_icon_name(input);
        return kenney != NULL ? kenney_icon(kenney) : NULL;
    }
    if (input >= KHDAYS_MOUSE_INPUT && input < KHDAYS_KEYBOARD_INPUTS) {
        return kenney_icon(mouse[input - KHDAYS_MOUSE_INPUT]);
    }
    if (input < 0 || input >= KHDAYS_MOUSE_INPUT) {
        return NULL;
    }
    snprintf(name, sizeof(name), "key:%d", input);
    return find_icon(name);
}

/* The icon of the first of `action`'s inputs in `layer` on the device played that has one. */
static const Icon *action_icon(const KhdaysProfile *profile, SDL_Gamepad *pad, int layer, int action)
{
    const int16_t *inputs = profile->input[pad != NULL ? KHDAYS_DEVICE_GAMEPAD : KHDAYS_DEVICE_KEYBOARD][layer][action];
    const Icon *icon = NULL;
    for (int k = 0; k < KHDAYS_INPUTS_PER_ACTION && icon == NULL; ++k) {
        if (inputs[k] != KHDAYS_NO_INPUT) {
            icon = input_icon(pad, inputs[k]);
        }
    }
    return icon;
}

/* The stick that drives the DS's D-pad, with `way` marked ("up", "horizontal"...; NULL none). */
static const Icon *stick_icon(const KhdaysProfile *profile, SDL_Gamepad *pad, const char *way)
{
    const char *f = family(pad);
    char name[64];
    for (int s = 0; s < 2; ++s) {
        if (profile->stick[s].role == KHDAYS_STICK_DPAD) {
            snprintf(name, sizeof(name), "%s_stick_%c%s%s", f != NULL ? f : "xbox", s == 0 ? 'l' : 'r',
                     way != NULL ? "_" : "", way != NULL ? way : "");
            return kenney_icon(name);
        }
    }
    return NULL;
}

/* The D-pad whole or two of its ways on the gamepad: the D-pad's when the DS's D-pad is on it,
 * else the stick that drives it. */
static const Icon *dpad_icon(const KhdaysProfile *profile, SDL_Gamepad *pad, int button)
{
    const char *f = family(pad);
    char name[64];
    int on_dpad = 0;
    if (f == NULL) {
        f = "xbox"; /* the Controls section's own default */
    }
    for (int a = KHDAYS_ACT_RIGHT; a <= KHDAYS_ACT_DOWN; ++a) {
        for (int k = 0; k < KHDAYS_INPUTS_PER_ACTION; ++k) {
            const int input = profile->input[KHDAYS_DEVICE_GAMEPAD][KHDAYS_LAYER_BASE][a][k];
            on_dpad |= input >= KHDAYS_PAD_DPUP && input <= KHDAYS_PAD_DPRIGHT;
        }
    }
    if (on_dpad) {
        snprintf(name, sizeof(name), "%s_dpad_%s", f,
                 button == KHDAYS_BUTTON_DPAD_HORIZONTAL ? "horizontal"
                 : button == KHDAYS_BUTTON_DPAD_VERTICAL ? "vertical"
                                                         : "none");
        return kenney_icon(name);
    }
    return stick_icon(profile, pad,
                      button == KHDAYS_BUTTON_DPAD_HORIZONTAL ? "horizontal"
                      : button == KHDAYS_BUTTON_DPAD_VERTICAL ? "vertical"
                                                              : NULL);
}

const uint8_t *khdays_button_icon(int button, int field, int *width, int *height)
{
    static const char *const ways[KHDAYS_BUTTON_COUNT] = {
        [KHDAYS_BUTTON_UP] = "up",
        [KHDAYS_BUTTON_DOWN] = "down",
        [KHDAYS_BUTTON_LEFT] = "left",
        [KHDAYS_BUTTON_RIGHT] = "right",
        [KHDAYS_BUTTON_DPAD_HORIZONTAL] = "horizontal",
        [KHDAYS_BUTTON_DPAD_VERTICAL] = "vertical",
    };
    static const int actions[KHDAYS_BUTTON_COUNT] = {
        [KHDAYS_BUTTON_A] = KHDAYS_ACT_A,           [KHDAYS_BUTTON_B] = KHDAYS_ACT_B,
        [KHDAYS_BUTTON_X] = KHDAYS_ACT_X,           [KHDAYS_BUTTON_Y] = KHDAYS_ACT_Y,
        [KHDAYS_BUTTON_L] = KHDAYS_ACT_L,           [KHDAYS_BUTTON_R] = KHDAYS_ACT_R,
        [KHDAYS_BUTTON_START] = KHDAYS_ACT_START,   [KHDAYS_BUTTON_SELECT] = KHDAYS_ACT_SELECT,
        [KHDAYS_BUTTON_TARGET_NEXT] = KHDAYS_ACT_R, /* the DS's way */
        [KHDAYS_BUTTON_UP] = KHDAYS_ACT_UP,         [KHDAYS_BUTTON_DOWN] = KHDAYS_ACT_DOWN,
        [KHDAYS_BUTTON_LEFT] = KHDAYS_ACT_LEFT,     [KHDAYS_BUTTON_RIGHT] = KHDAYS_ACT_RIGHT,
    };
    const KhdaysProfile *profile = khdays_devices_profile();
    SDL_Gamepad *pad = khdays_devices_pad_in_use() ? khdays_devices_gamepad() : NULL;
    const Icon *icon = NULL;
    if (profile == NULL || button <= KHDAYS_BUTTON_NONE || button >= KHDAYS_BUTTON_COUNT) {
        return NULL;
    }
    if (!layouts_read) {
        read_layouts();
    }
    /* in the field with the D-pad command menu on: the commands cycle by that menu's input, and
       the DS's D-pad (moving, the free camera) is the stick; with the separate lock-on, R is its */
    const int menu = field && profile->enh.dpad_command_menu;
    if (menu && button == KHDAYS_BUTTON_X) {
        icon = action_icon(profile, pad, KHDAYS_LAYER_FIELD, KHDAYS_ACT_MENU_DOWN);
    } else if (menu && pad != NULL && button >= KHDAYS_BUTTON_UP) {
        icon = stick_icon(profile, pad, ways[button]);
    } else if (field && profile->enh.separate_lock_on &&
               (button == KHDAYS_BUTTON_R || button == KHDAYS_BUTTON_TARGET_NEXT)) {
        icon = action_icon(profile, pad, KHDAYS_LAYER_BASE,
                           button == KHDAYS_BUTTON_R ? KHDAYS_ACT_LOCK_ON : KHDAYS_ACT_TARGET_NEXT);
    }
    if (icon == NULL) {
        icon = button < KHDAYS_BUTTON_DPAD ? action_icon(profile, pad, KHDAYS_LAYER_BASE, actions[button])
               : pad != NULL               ? dpad_icon(profile, pad, button)
                                           : NULL;
    }
    if (icon == NULL || icon->rgba == NULL) {
        return NULL;
    }
    *width = icon->width;
    *height = icon->height;
    return icon->rgba;
}

uint32_t khdays_button_icons_signature(void)
{
    const KhdaysProfile *profile = khdays_devices_profile();
    SDL_Gamepad *pad = khdays_devices_pad_in_use() ? khdays_devices_gamepad() : NULL;
    const int device = pad != NULL ? KHDAYS_DEVICE_GAMEPAD : KHDAYS_DEVICE_KEYBOARD;
    uint32_t h = 2166136261u;
    h = (h ^ (pad != NULL ? (uint32_t)SDL_GetGamepadID(pad) : 0u)) * 16777619u;
    h = (h ^ (pad != NULL ? (uint32_t)SDL_GetGamepadType(pad) : 0u)) * 16777619u;
    if (profile != NULL) {
        for (int layer = 0; layer < KHDAYS_LAYER_COUNT; ++layer) {
            for (int a = 0; a < KHDAYS_ACT_COUNT; ++a) {
                for (int k = 0; k < KHDAYS_INPUTS_PER_ACTION; ++k) {
                    h = (h ^ (uint16_t)profile->input[device][layer][a][k]) * 16777619u;
                }
            }
        }
        h = (h ^ (uint32_t)profile->stick[0].role << 4 ^ (uint32_t)profile->stick[1].role) * 16777619u;
        h = (h ^ (uint32_t)profile->enh.dpad_command_menu << 1 ^ (uint32_t)profile->enh.separate_lock_on) * 16777619u;
    }
    return h;
}
