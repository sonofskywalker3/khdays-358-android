/* touch_overlay.h. Everything is drawn from triangles and the game's font, half see-through, over the letterboxed
 * picture in the window's own pixels (as the settings menu draws). On the PC, KHDAYS_TOUCH_PREVIEW shows the
 * controls for checking them: "<pixels a point> <a|c> <window width> <window height>", the mouse a finger. */
#include "touch_overlay.h"

#include "touch_input.h"
#include "../android/touch_gear.h"
#include "../app/devices.h"
#include "../render/gpu_present.h"
#include "../render/onescreen.h"
#include "../screens/movie_pause.h"
#include "../screens/ui_font.h"
#include "../settings/settings_ui.h"
#include "../../runtime/movies/movie_hd.h"
#include "../../ui/composition.h"
#include "../../ui/situation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STEPS 48
#define PI_F 3.14159265f

#define STICK_ZONE 0.5f       /* of the window's width: a finger landing left of it is the stick */
#define TAP_TRAVEL 30.0f      /* points: a tap in an event dragged further is a direction */
#define COUNTS_A_POINT 2.1f   /* the mouse camera's counts for a point dragged: the window's width half a turn */
#define STICK_DEAD 25         /* percent */
#define STICK_HYSTERESIS 5
#define DEFAULT_DENSITY 2.0f  /* points when the system says nothing */
#define MOUSE_FINGER 1u       /* the preview's mouse */
#define DECK_ROW_FIRST 59.0f  /* the deck's first row in its shape, source dots under its top */
#define DECK_ROW_HEIGHT 15.7f
#define DECK_ROW_WIDTH 87.0f
#define BUZZ_MS 15
#define BUZZ_STRENGTH 0.5f

static KhdaysTouchInput input;
static KhdaysTouchScreen screen_now;
static int layout_now = -1;
static int hud_published;
static int editing;           /* the layout editor has the places */
static int shown;             /* the controls are on the screen */
static int touched_last;      /* the screen touched since the gamepad was last used */
static int mouse_is_ours;     /* the touch's mouse press went to the controls: so do its motion and release */
static uint16_t last_buttons; /* for the vibration */
static SDL_Haptic *haptic;
static int haptic_tried;

/* The preview's settings: density 0 when off. */
static struct {
    int read;
    float density;
    int layout, w, h;
    int sized;
} preview;

static void read_preview(void)
{
    const char *e;
    char which = 'c';
    if (preview.read) {
        return;
    }
    preview.read = 1;
#if !defined(__ANDROID__)
    e = SDL_getenv("KHDAYS_TOUCH_PREVIEW");
    if (e != NULL && sscanf(e, "%f %c %d %d", &preview.density, &which, &preview.w, &preview.h) >= 2) {
        preview.layout = which == 'a' ? KHDAYS_TOUCH_LAYOUT_A : KHDAYS_TOUCH_LAYOUT_C;
    } else {
        preview.density = 0.0f;
    }
#else
    (void)e;
    (void)which;
#endif
}

/* What the controls are for now. */
static int mode_now(void)
{
    const int scene = khdays_onescreen_scene();
    int row, skip;
    if (khdays_settings_is_open() || khdays_movie_pause_menu(&row, &skip)) {
        return KHDAYS_TOUCH_MODE_NONE;
    }
    if (khdays_movie_hd_showing()) {
        return KHDAYS_TOUCH_MODE_TALK;
    }
    switch (scene) {
    case KHDAYS_SIT_FIELD:
    case KHDAYS_SIT_FIELD_3D_BOTH:
        return (khdays_onescreen_states() & (1u << KHDAYS_ST_HUD)) ? KHDAYS_TOUCH_MODE_FIELD : KHDAYS_TOUCH_MODE_TALK;
    case KHDAYS_SIT_MOVIE:
    case KHDAYS_SIT_INTRO:
    case KHDAYS_SIT_DAY_COUNTER:
    case KHDAYS_SIT_THOUGHTS:
    case KHDAYS_SIT_THE_END: return KHDAYS_TOUCH_MODE_TALK;
    default: return KHDAYS_TOUCH_MODE_MENU;
    }
}

/* Whether the player's settings show the controls now. */
static int wanted(void)
{
    const KhdaysProfile *p = khdays_devices_profile();
    if (preview.density > 0.0f) {
        return 1;
    }
#if defined(__ANDROID__)
    if (p->touch.mode == KHDAYS_TOUCH_OFF || !khdays_present_one_picture()) {
        return 0;
    }
    if (p->touch.mode == KHDAYS_TOUCH_WITHOUT_PAD && khdays_devices_gamepad() != NULL &&
        (khdays_devices_pad_in_use() || !touched_last)) {
        touched_last = 0;
        return 0;
    }
    return 1;
#else
    (void)p;
    return 0;
#endif
}

static void hide(void)
{
    khdays_touch_input_mode(&input, KHDAYS_TOUCH_MODE_NONE);
    shown = 0;
    if (hud_published) {
        const KhdaysProfile *p = khdays_devices_profile();
        khdays_hud_layout_set(p->hud_layout.custom ? p->hud_layout.place : NULL); /* the player's, or the composition's */
        hud_published = 0;
    }
}

int khdays_touch_available(void)
{
    read_preview();
#if defined(__ANDROID__)
    return khdays_present_one_picture();
#else
    return preview.density > 0.0f;
#endif
}

float khdays_touch_density(SDL_Renderer *renderer)
{
    float density;
    read_preview();
    density = preview.density > 0.0f ? preview.density : SDL_GetWindowDisplayScale(SDL_GetRenderWindow(renderer));
    return density > 0.0f ? density : DEFAULT_DENSITY;
}

void khdays_touch_edit(const KhdaysTouchLayout *layout)
{
    editing = layout != NULL;
    if (layout != NULL) {
        input.layout = *layout;
    } else {
        layout_now = -1;
    }
}

void khdays_touch_relayout(void)
{
    layout_now = -1;
    hud_published = 1; /* whatever hides next puts the player's own back */
}

/* Keeps the places in step with the window, the player's settings and the picture. */
static void place(SDL_Renderer *renderer)
{
    const KhdaysProfile *p = khdays_devices_profile();
    KhdaysTouchScreen s;
    int w = 0, h = 0;
    int layout;
    const float density = khdays_touch_density(renderer);
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    memset(&s, 0, sizeof(s));
    s.w = (float)w;
    s.h = (float)h;
    s.density = density;
    s.size = (float)p->touch.size / 100.0f;
    s.columns = khdays_present_columns();
    s.hud = khdays_composition_hud_factor();
    layout = preview.density > 0.0f ? preview.layout : p->touch.layout;
    if (memcmp(&s, &screen_now, sizeof(s)) != 0 || layout != layout_now) {
        screen_now = s;
        layout_now = layout;
        if (layout == KHDAYS_TOUCH_LAYOUT_CUSTOM) {
            khdays_touch_layout_custom(&s, p->hud_layout.custom ? p->hud_layout.place : NULL, p->touch.control, &input.layout);
        } else {
            khdays_touch_layout_make(layout, &s, &input.layout);
        }
        input.stick_radius = (input.layout.box[KHDAYS_TOUCH_STICK].x1 - input.layout.box[KHDAYS_TOUCH_STICK].x0) / 2.0f;
        input.stick_zone = s.w * STICK_ZONE;
        input.tap_travel = TAP_TRAVEL * density;
        khdays_hud_layout_set(input.layout.hud);
        hud_published = 1;
    } else if (!hud_published) {
        khdays_hud_layout_set(input.layout.hud);
        hud_published = 1;
    }
    /* the deck's rows, where the last picture put its shape */
    {
        KhdaysHudBox box;
        float scale;
        memset(input.deck_rows, 0, sizeof(input.deck_rows));
        if (input.mode == KHDAYS_TOUCH_MODE_FIELD && khdays_hud_layout_box(KHDAYS_HUD_DECK, screen_now.columns, &box, &scale)) {
            const float d = input.layout.dot, ox = input.layout.dots_x, oy = input.layout.dots_y;
            for (int r = 0; r < KHDAYS_TOUCH_DECK_ROWS; ++r) {
                KhdaysTouchBox *b = &input.deck_rows[r];
                b->x0 = ox + box.x0 * d;
                b->x1 = ox + (box.x0 + DECK_ROW_WIDTH * scale) * d;
                b->y0 = oy + (box.y0 + (DECK_ROW_FIRST + DECK_ROW_HEIGHT * (float)r) * scale) * d;
                b->y1 = b->y0 + DECK_ROW_HEIGHT * scale * d;
            }
        }
    }
}

/* ---- Drawing ------------------------------------------------------------- */

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

/* A band along a closed line of points, `width` wide inwards towards (cx, cy). */
static void band(SDL_Renderer *renderer, const SDL_FPoint *line, int n, float cx, float cy, float width, SDL_FColor colour)
{
    SDL_Vertex v[(STEPS + 8) * 2];
    int indices[(STEPS + 8) * 6];
    for (int i = 0; i < n; ++i) {
        const float dx = cx - line[i].x, dy = cy - line[i].y, far = hypotf(dx, dy);
        const float k = far > 0.0f ? width / far : 0.0f;
        v[2 * i].position = line[i];
        v[2 * i + 1].position.x = line[i].x + dx * k;
        v[2 * i + 1].position.y = line[i].y + dy * k;
        v[2 * i].color = v[2 * i + 1].color = colour;
        v[2 * i].tex_coord.x = v[2 * i].tex_coord.y = 0.0f;
        v[2 * i + 1].tex_coord = v[2 * i].tex_coord;
    }
    for (int i = 0; i < n; ++i) {
        const int a = 2 * i, b = 2 * ((i + 1) % n);
        int *t = &indices[6 * i];
        t[0] = a;
        t[1] = a + 1;
        t[2] = b + 1;
        t[3] = a;
        t[4] = b + 1;
        t[5] = b;
    }
    SDL_RenderGeometry(renderer, NULL, v, 2 * n, indices, 6 * n);
}

static void ring(SDL_Renderer *renderer, float cx, float cy, float r, float width, SDL_FColor colour)
{
    SDL_FPoint line[STEPS];
    for (int i = 0; i < STEPS; ++i) {
        const float a = 2.0f * PI_F * (float)i / STEPS;
        line[i].x = cx + r * cosf(a);
        line[i].y = cy + r * sinf(a);
    }
    band(renderer, line, STEPS, cx, cy, width, colour);
}

/* A rectangle with half-round ends. */
static void pill(SDL_Renderer *renderer, const KhdaysTouchBox *b, SDL_FColor fill, SDL_FColor edge, float width)
{
    const float r = (b->y1 - b->y0) / 2.0f, cy = (b->y0 + b->y1) / 2.0f;
    const int half = STEPS / 2;
    {
        /* one shape, so the see-through colour lies once everywhere */
        SDL_Vertex v[STEPS + 1];
        int indices[STEPS * 3];
        memset(v, 0, sizeof(v));
        v[STEPS].position.x = (b->x0 + b->x1) / 2.0f;
        v[STEPS].position.y = cy;
        v[STEPS].color = fill;
        for (int i = 0; i < half; ++i) {
            const float a = PI_F * (float)i / (float)(half - 1);
            v[i].position.x = b->x1 - r + r * sinf(a);
            v[i].position.y = cy - r * cosf(a);
            v[half + i].position.x = b->x0 + r - r * sinf(a);
            v[half + i].position.y = cy + r * cosf(a);
            v[i].color = v[half + i].color = fill;
        }
        for (int i = 0; i < STEPS; ++i) {
            indices[3 * i] = STEPS;
            indices[3 * i + 1] = i;
            indices[3 * i + 2] = (i + 1) % STEPS;
        }
        SDL_RenderGeometry(renderer, NULL, v, STEPS + 1, indices, STEPS * 3);
    }
    {
        SDL_SetRenderDrawColorFloat(renderer, edge.r, edge.g, edge.b, edge.a);
        const SDL_FRect top = {b->x0 + r, b->y0, b->x1 - b->x0 - 2.0f * r, width};
        const SDL_FRect foot = {b->x0 + r, b->y1 - width, b->x1 - b->x0 - 2.0f * r, width};
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &foot);
    }
    {
        SDL_FPoint left[STEPS / 2], right[STEPS / 2];
        for (int i = 0; i < half; ++i) {
            const float a = PI_F * (float)i / (float)(half - 1);
            right[i].x = r * sinf(a);
            right[i].y = -r * cosf(a);
            left[i].x = -right[i].x;
            left[i].y = right[i].y;
        }
        for (int i = 0; i + 1 < half; ++i) {
            const float k = r > width ? (r - width) / r : 0.0f;
            const float xs[2] = {b->x1 - r, b->x0 + r};
            const SDL_FPoint *arc[2] = {right, left};
            for (int side = 0; side < 2; ++side) {
                SDL_Vertex v[4];
                static const int order[6] = {0, 1, 2, 1, 3, 2};
                const SDL_FPoint p0 = arc[side][i], p1 = arc[side][i + 1];
                memset(v, 0, sizeof(v));
                v[0].position.x = xs[side] + p0.x;
                v[0].position.y = cy + p0.y;
                v[1].position.x = xs[side] + p1.x;
                v[1].position.y = cy + p1.y;
                v[2].position.x = xs[side] + p0.x * k;
                v[2].position.y = cy + p0.y * k;
                v[3].position.x = xs[side] + p1.x * k;
                v[3].position.y = cy + p1.y * k;
                v[0].color = v[1].color = v[2].color = v[3].color = edge;
                SDL_RenderGeometry(renderer, NULL, v, 4, order, 6);
            }
        }
    }
}

static void label(SDL_Renderer *renderer, float cx, float cy, float height, float alpha, const char *s)
{
    int w = 0, h = 0;
    SDL_Texture *t = khdays_ui_font_load() ? khdays_ui_font_text(renderer, s, &w, &h) : NULL;
    if (t != NULL && h > 0) {
        const float k = height / (float)h;
        const SDL_FRect dst = {cx - (float)w * k / 2.0f, cy - height / 2.0f, (float)w * k, height};
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureColorMod(t, 255, 255, 255);
        SDL_SetTextureAlphaModFloat(t, alpha);
        SDL_RenderTexture(renderer, t, NULL, &dst);
        SDL_SetTextureAlphaModFloat(t, 1.0f);
    }
}

typedef struct Ink {
    SDL_FColor fill, pressed, edge;
    float text;
    float line;
} Ink;

static void pill_control(SDL_Renderer *renderer, const Ink *ink, int control, unsigned held, const char *s)
{
    const KhdaysTouchBox *b = &input.layout.box[control];
    pill(renderer, b, (held >> control) & 1u ? ink->pressed : ink->fill, ink->edge, ink->line);
    label(renderer, (b->x0 + b->x1) / 2.0f, (b->y0 + b->y1) / 2.0f, (b->y1 - b->y0) * 0.55f, ink->text, s);
}

static void draw_face(SDL_Renderer *renderer, const Ink *ink)
{
    static const char *const names[4] = {"A", "B", "X", "Y"};
    static const int bits[4] = {KHDAYS_ACT_A, KHDAYS_ACT_B, KHDAYS_ACT_X, KHDAYS_ACT_Y};
    for (int i = 0; i < 4; ++i) {
        float x, y, r;
        khdays_touch_face_button(&input.layout.box[KHDAYS_TOUCH_FACE], i, &x, &y, &r);
        disc(renderer, x, y, r, (input.buttons >> bits[i]) & 1u ? ink->pressed : ink->fill);
        ring(renderer, x, y, r, ink->line, ink->edge);
        label(renderer, x, y, r, ink->text, names[i]);
    }
}

static void draw_stick(SDL_Renderer *renderer, const Ink *ink)
{
    const KhdaysTouchBox *b = &input.layout.box[KHDAYS_TOUCH_STICK];
    float cx = (b->x0 + b->x1) / 2.0f, cy = (b->y0 + b->y1) / 2.0f;
    const float r = input.stick_radius;
    float knob_x = cx, knob_y = cy;
    if (input.stick_held) {
        for (int i = 0; i < KHDAYS_TOUCH_FINGERS; ++i) {
            const KhdaysTouchFinger *f = &input.finger[i];
            if (f->job != 0 && f->x0 < input.stick_zone && hypotf(f->x - f->x0, f->y - f->y0) <= r + 1.0f &&
                (input.stick_x == 0.0f || (f->x - f->x0) / r == input.stick_x)) {
                cx = f->x0;
                cy = f->y0;
                knob_x = f->x;
                knob_y = f->y;
                break;
            }
        }
    }
    disc(renderer, cx, cy, r, ink->fill);
    ring(renderer, cx, cy, r, ink->line, ink->edge);
    disc(renderer, knob_x, knob_y, r * 0.45f, input.stick_held ? ink->pressed : ink->edge);
    ring(renderer, knob_x, knob_y, r * 0.45f, ink->line, ink->edge);
}

static void draw_dpad(SDL_Renderer *renderer, const Ink *ink)
{
    const KhdaysTouchBox *b = &input.layout.box[KHDAYS_TOUCH_DPAD];
    const float w = b->x1 - b->x0, arm = w * 0.34f;
    const float cx = (b->x0 + b->x1) / 2.0f, cy = (b->y0 + b->y1) / 2.0f;
    const SDL_FRect across = {b->x0, cy - arm / 2.0f, w, arm};
    const SDL_FRect down = {cx - arm / 2.0f, b->y0, arm, w};
    static const int bits[4] = {KHDAYS_ACT_UP, KHDAYS_ACT_RIGHT, KHDAYS_ACT_DOWN, KHDAYS_ACT_LEFT};
    SDL_SetRenderDrawColorFloat(renderer, ink->fill.r, ink->fill.g, ink->fill.b, ink->fill.a);
    SDL_RenderFillRect(renderer, &across);
    SDL_RenderFillRect(renderer, &down);
    for (int i = 0; i < 4; ++i) {
        /* a triangle on each arm, bright while that direction is held */
        const float a = (float)i * PI_F / 2.0f - PI_F / 2.0f;
        const float ux = cosf(a), uy = sinf(a);
        const float tip = w * 0.46f, base = w * 0.30f, wide = arm * 0.32f;
        const SDL_FColor c = (input.buttons >> bits[i]) & 1u ? ink->pressed : ink->edge;
        SDL_Vertex v[3];
        memset(v, 0, sizeof(v));
        v[0].position.x = cx + ux * tip;
        v[0].position.y = cy + uy * tip;
        v[1].position.x = cx + ux * base - uy * wide;
        v[1].position.y = cy + uy * base + ux * wide;
        v[2].position.x = cx + ux * base + uy * wide;
        v[2].position.y = cy + uy * base - ux * wide;
        v[0].color = v[1].color = v[2].color = c;
        SDL_RenderGeometry(renderer, NULL, v, 3, NULL, 0);
    }
}

static void draw_lock(SDL_Renderer *renderer, const Ink *ink, unsigned held)
{
    const KhdaysTouchBox *b = &input.layout.box[KHDAYS_TOUCH_LOCK];
    const float r = (b->x1 - b->x0) / 2.0f, cx = b->x0 + r, cy = b->y0 + r;
    const float w = ink->line;
    const SDL_FRect across = {cx - r * 0.72f, cy - w / 2.0f, r * 1.44f, w};
    const SDL_FRect down = {cx - w / 2.0f, cy - r * 0.72f, w, r * 1.44f};
    disc(renderer, cx, cy, r, (held >> KHDAYS_TOUCH_LOCK) & 1u ? ink->pressed : ink->fill);
    ring(renderer, cx, cy, r, w, ink->edge);
    ring(renderer, cx, cy, r * 0.5f, w, ink->edge);
    SDL_SetRenderDrawColorFloat(renderer, ink->edge.r, ink->edge.g, ink->edge.b, ink->edge.a);
    SDL_RenderFillRect(renderer, &across);
    SDL_RenderFillRect(renderer, &down);
}

int khdays_touch_draw(SDL_Renderer *renderer)
{
    SDL_RendererLogicalPresentation mode;
    SDL_BlendMode blend;
    int lw = 0, lh = 0, situation;
    read_preview();
    if (renderer == NULL || (!wanted() && !editing)) {
        hide();
        return 0;
    }
    if (preview.density > 0.0f && !preview.sized && preview.w > 0 && preview.h > 0) {
        SDL_SetWindowSize(SDL_GetRenderWindow(renderer), preview.w, preview.h);
        preview.sized = 1;
    }
    situation = editing ? KHDAYS_TOUCH_MODE_FIELD : mode_now();
    khdays_touch_input_mode(&input, situation);
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    if (!editing) {
        place(renderer);
    }
    shown = situation != KHDAYS_TOUCH_MODE_NONE;
    if (shown) {
        const float alpha = (float)khdays_devices_profile()->touch.opacity / 100.0f;
        const unsigned held = khdays_touch_input_pressed(&input);
        const float line = screen_now.density * screen_now.size * 1.5f;
        const Ink ink = {
            {0.20f, 0.11f, 0.07f, 0.90f * alpha},
            {1.00f, 0.82f, 0.45f, 0.85f * alpha},
            {1.00f, 1.00f, 1.00f, 0.90f * alpha},
            alpha >= 0.5f ? 1.0f : alpha * 2.0f,
            line < 1.0f ? 1.0f : line,
        };
        SDL_GetRenderDrawBlendMode(renderer, &blend);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        if (situation == KHDAYS_TOUCH_MODE_TALK) {
            pill_control(renderer, &ink, KHDAYS_TOUCH_START, held, "START");
        } else {
            const KhdaysTouchBox *g = &input.layout.box[KHDAYS_TOUCH_GEAR];
            if (situation == KHDAYS_TOUCH_MODE_FIELD) {
                draw_stick(renderer, &ink);
                draw_lock(renderer, &ink, held);
            } else {
                draw_dpad(renderer, &ink);
            }
            pill_control(renderer, &ink, KHDAYS_TOUCH_L, held, "L");
            pill_control(renderer, &ink, KHDAYS_TOUCH_R, held, "R");
            pill_control(renderer, &ink, KHDAYS_TOUCH_SELECT, held, "SELECT");
            pill_control(renderer, &ink, KHDAYS_TOUCH_START, held, "START");
            draw_face(renderer, &ink);
            disc(renderer, (g->x0 + g->x1) / 2.0f, (g->y0 + g->y1) / 2.0f, (g->x1 - g->x0) / 2.0f,
                 (held >> KHDAYS_TOUCH_GEAR) & 1u ? ink.pressed : ink.fill);
            khdays_touch_gear_shape(renderer, (g->x0 + g->x1) / 2.0f, (g->y0 + g->y1) / 2.0f,
                                    (g->x1 - g->x0) * 0.36f, ink.edge);
        }
        SDL_SetRenderDrawBlendMode(renderer, blend);
    }
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
    return 1;
}

/* ---- Fingers ------------------------------------------------------------- */

/* A short buzz as a press begins, the system's vibrator (Android only: nothing to shake on a PC). */
static void buzz(void)
{
    const uint16_t now = input.buttons;
    const int pressed = (now & ~last_buttons) != 0;
    last_buttons = now;
#if defined(__ANDROID__)
    if (pressed && khdays_devices_profile()->touch.vibration) {
        if (!haptic_tried) {
            int count = 0;
            SDL_HapticID *ids;
            haptic_tried = 1;
            if (SDL_InitSubSystem(SDL_INIT_HAPTIC) && (ids = SDL_GetHaptics(&count)) != NULL) {
                if (count > 0 && (haptic = SDL_OpenHaptic(ids[0])) != NULL && !SDL_InitHapticRumble(haptic)) {
                    SDL_CloseHaptic(haptic);
                    haptic = NULL;
                }
                SDL_free(ids);
            }
        }
        if (haptic != NULL) {
            SDL_PlayHapticRumble(haptic, BUZZ_STRENGTH, BUZZ_MS);
        }
    }
#else
    (void)pressed;
    (void)haptic;
    (void)haptic_tried;
#endif
}

int khdays_touch_event(const SDL_Event *event, SDL_Renderer *renderer)
{
    read_preview();
    if (editing) {
        return 0;
    }
    switch (event->type) {
    case SDL_EVENT_FINGER_DOWN:
        touched_last = 1; /* the controls come back at the first touch since the gamepad */
        /* fall through */
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (!shown) {
            return 0;
        }
        {
            const float x = event->tfinger.x * screen_now.w, y = event->tfinger.y * screen_now.h;
            const uint64_t id = (uint64_t)event->tfinger.fingerID;
            if (event->type == SDL_EVENT_FINGER_DOWN) {
                khdays_touch_input_down(&input, id, x, y);
            } else if (event->type == SDL_EVENT_FINGER_MOTION) {
                khdays_touch_input_move(&input, id, x, y);
            } else {
                khdays_touch_input_up(&input, id);
            }
            buzz();
            khdays_devices_resend();
            if (input.tap_pulse) {
                input.tap_pulse = 0; /* its press and release both seen: press_edges.h counts it once */
                input.buttons &= (uint16_t)~(1u << KHDAYS_ACT_A);
                khdays_devices_resend();
            }
        }
        return 1;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION: {
        const int preview_mouse = preview.density > 0.0f && event->button.which != SDL_TOUCH_MOUSEID;
        const int from_touch = event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.which == SDL_TOUCH_MOUSEID
                                                                     : event->button.which == SDL_TOUCH_MOUSEID;
        SDL_Window *const window = SDL_GetRenderWindow(renderer);
        const float k = SDL_GetWindowPixelDensity(window) > 0.0f ? SDL_GetWindowPixelDensity(window) : 1.0f;
        const float x = (event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.x : event->button.x) * k;
        const float y = (event->type == SDL_EVENT_MOUSE_MOTION ? event->motion.y : event->button.y) * k;
        if (!shown || (!from_touch && !preview_mouse)) {
            return 0;
        }
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            mouse_is_ours = khdays_touch_input_wants(&input, x, y);
        }
        if (mouse_is_ours && preview_mouse) {
            if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                khdays_touch_input_down(&input, MOUSE_FINGER, x, y);
            } else if (event->type == SDL_EVENT_MOUSE_MOTION) {
                khdays_touch_input_move(&input, MOUSE_FINGER, x, y);
            } else {
                khdays_touch_input_up(&input, MOUSE_FINGER);
            }
            khdays_devices_resend();
            if (input.tap_pulse) {
                input.tap_pulse = 0;
                input.buttons &= (uint16_t)~(1u << KHDAYS_ACT_A);
                khdays_devices_resend();
            }
        }
        {
            const int ours = mouse_is_ours;
            if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
                mouse_is_ours = 0;
            }
            return ours;
        }
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        khdays_touch_release();
        return 0;
    default: return 0;
    }
}

int khdays_touch_take_settings(void)
{
    const int asked = input.settings;
    input.settings = 0;
    return asked;
}

void khdays_touch_release(void)
{
    khdays_touch_input_release(&input);
    mouse_is_ours = 0;
    last_buttons = 0;
}

void khdays_touch_apply(const KhdaysProfile *profile, KhdaysInputSnapshot *out)
{
    static int stick_active, stick_sector = -1;
    if (input.mode == KHDAYS_TOUCH_MODE_NONE || editing) {
        return;
    }
    out->ds_buttons |= input.buttons;
    out->ds_buttons_field |= input.buttons;
    if (input.mode == KHDAYS_TOUCH_MODE_FIELD) {
        const KhdaysStick stick = {KHDAYS_STICK_DPAD, STICK_DEAD, STICK_HYSTERESIS};
        const int sx = (int)(fmaxf(-1.0f, fminf(1.0f, input.stick_x)) * 32767.0f);
        const int sy = (int)(fmaxf(-1.0f, fminf(1.0f, input.stick_y)) * 32767.0f);
        const uint16_t bits = khdays_stick_dpad(sx, sy, &stick, &stick_active, &stick_sector);
        const double counts = (double)COUNTS_A_POINT / (double)screen_now.density;
        out->ds_buttons |= bits;
        out->ds_buttons_field |= bits;
        if (profile->enh.analog_movement && bits != 0) {
            out->move_x = (int16_t)sx;
            out->move_y = (int16_t)-sy;
        }
        if (input.lock_on) {
            out->actions |= KHDAYS_ACTION_LOCK_ON;
        }
        /* the fingers' own camera and lock-on, whatever the settings say for the gamepad */
        out->enhancements |= KHDAYS_ENH_SEPARATE_LOCK_ON | KHDAYS_ENH_MOUSE_CAMERA;
        out->mouse_x += (int32_t)floor(input.camera_x * counts * (profile->enh.camera_invert_x ? -1.0 : 1.0));
        out->mouse_y += (int32_t)floor(input.camera_y * counts * (profile->enh.camera_invert_y ? 1.0 : -1.0));
        if (!profile->enh.mouse_camera) {
            out->mouse_speed = 100;
        }
        out->deck_tap_row = input.deck_row;
        out->deck_tap_serial = input.deck_serial;
    }
}
