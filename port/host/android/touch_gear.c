/* touch_gear.h: the gear is drawn from triangles (no picture of it to load), white over a dark disc,
 * half see-through so the corner of the game shows under it. */
#include "touch_gear.h"

#include <math.h>

#include "../app/devices.h"
#include "../render/gpu_present.h"
#include "../settings/settings_ui.h"

#define TEETH 8
#define STEPS (TEETH * 12)             /* points around the gear's edge */
#define SIZE_OF_HEIGHT 0.09f           /* the gear's width, of the window's shorter side */
#define MARGIN_OF_SIZE 0.35f           /* its distance from the corner, of its width */
#define TOOTH_SHARE 0.45f              /* the share of each tooth's step its tooth takes */
#define ROOT_OF_OUTER 0.74f            /* the radius between the teeth, of the teeth's */
#define HOLE_OF_OUTER 0.32f            /* the hole's */
#define DISC_OF_OUTER 1.30f            /* the dark disc behind it */
#define REACH_OF_OUTER 1.60f           /* how far from its centre a tap still hits it */
#define PI_F 3.14159265f

static float shown_x, shown_y, shown_r; /* the gear drawn last: its centre and radius (0: none) */

static void disc(SDL_Renderer *renderer, float cx, float cy, float r, SDL_FColor colour)
{
    SDL_Vertex v[STEPS + 1];
    int indices[STEPS * 3];
    v[STEPS].position.x = cx;
    v[STEPS].position.y = cy;
    v[STEPS].color = colour;
    for (int i = 0; i < STEPS; ++i) {
        const float a = 2.0f * PI_F * (float)i / STEPS;
        v[i].position.x = cx + r * cosf(a);
        v[i].position.y = cy + r * sinf(a);
        v[i].color = colour;
        indices[3 * i] = STEPS;
        indices[3 * i + 1] = i;
        indices[3 * i + 2] = (i + 1) % STEPS;
    }
    SDL_RenderGeometry(renderer, NULL, v, STEPS + 1, indices, STEPS * 3);
}

static void gear(SDL_Renderer *renderer, float cx, float cy, float r, SDL_FColor colour)
{
    /* a ring from the hole out to the edge, the edge at the teeth's radius on each tooth */
    SDL_Vertex v[STEPS * 2];
    int indices[STEPS * 6];
    for (int i = 0; i < STEPS; ++i) {
        const float a = 2.0f * PI_F * ((float)i + 0.5f) / STEPS;
        const float in_tooth = fmodf((float)i + 0.5f, (float)STEPS / TEETH) / ((float)STEPS / TEETH);
        const float edge = in_tooth < TOOTH_SHARE ? r : r * ROOT_OF_OUTER;
        v[2 * i].position.x = cx + r * HOLE_OF_OUTER * cosf(a);
        v[2 * i].position.y = cy + r * HOLE_OF_OUTER * sinf(a);
        v[2 * i + 1].position.x = cx + edge * cosf(a);
        v[2 * i + 1].position.y = cy + edge * sinf(a);
        v[2 * i].color = colour;
        v[2 * i + 1].color = colour;
    }
    for (int i = 0; i < STEPS; ++i) {
        const int a = 2 * i, b = 2 * ((i + 1) % STEPS);
        int *t = &indices[6 * i];
        t[0] = a;
        t[1] = a + 1;
        t[2] = b + 1;
        t[3] = a;
        t[4] = b + 1;
        t[5] = b;
    }
    SDL_RenderGeometry(renderer, NULL, v, STEPS * 2, indices, STEPS * 6);
}

void khdays_touch_gear_draw(SDL_Renderer *renderer)
{
    SDL_RendererLogicalPresentation mode;
    SDL_BlendMode blend;
    int lw = 0, lh = 0, w = 0, h = 0;
    float size, r;
    shown_r = 0.0f;
    if (renderer == NULL || khdays_settings_is_open() || khdays_devices_pad_in_use() ||
        khdays_present_settings_button()) { /* the title's HD menu has a settings key of its own */
        return;
    }
    /* in the window's own pixels, over the letterboxed picture (as the settings menu draws) */
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    if (w > 0 && h > 0) {
        size = (float)(w < h ? w : h) * SIZE_OF_HEIGHT;
        r = size / 2.0f;
        shown_x = (float)w - size * MARGIN_OF_SIZE - r;
        shown_y = size * MARGIN_OF_SIZE + r;
        shown_r = r;
        SDL_GetRenderDrawBlendMode(renderer, &blend);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        disc(renderer, shown_x, shown_y, r * DISC_OF_OUTER, (SDL_FColor){0.0f, 0.0f, 0.0f, 0.35f});
        gear(renderer, shown_x, shown_y, r, (SDL_FColor){1.0f, 1.0f, 1.0f, 0.6f});
        SDL_SetRenderDrawBlendMode(renderer, blend);
    }
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
}

int khdays_touch_gear_at(float x, float y)
{
    const float dx = x - shown_x, dy = y - shown_y, reach = shown_r * REACH_OF_OUTER;
    return shown_r > 0.0f && dx * dx + dy * dy <= reach * reach;
}
