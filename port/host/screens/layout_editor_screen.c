/* layout_editor_screen.h. The dragging itself is ui/layout_editor.c's; here are the items, the bar, the drawing, and
 * turning an item's new box into a place for the profile. */
#include "layout_editor_screen.h"

#include "ui_font.h"
#include "ui_text.h"
#include "../app/devices.h"
#include "../render/gpu_present.h"
#include "../touch/touch_layout.h"
#include "../touch/touch_overlay.h"
#include "../../ui/composition.h"
#include "../../ui/layout_editor.h"

#include <math.h>
#include <string.h>

enum { ITEM_HUD, ITEM_CONTROL };
enum { BAR_A, BAR_C, BAR_CUSTOM, BAR_SNAP, BAR_RESIZE, BAR_RESET, BAR_DONE, BAR_BUTTONS };

#define GRID_POINTS 12.0f      /* the snap's lines */
#define BAR_H_POINTS 34.0f
#define BAR_GAP_POINTS 6.0f
#define BAR_PAD_POINTS 11.0f
#define BAR_DOWN 0.28f         /* of the window's height */
#define DASH_POINTS 7.0f
#define LINE_POINTS 1.5f
#define LABEL_OF_BAR 0.55f
#define UI_ROWS 540.0f         /* a point of the bar at the least: the window's height over this */
#define NAME_POINTS 12.0f

static const int moved_controls[] = {KHDAYS_TOUCH_STICK, KHDAYS_TOUCH_L,    KHDAYS_TOUCH_R,      KHDAYS_TOUCH_FACE,
                                     KHDAYS_TOUCH_LOCK,  KHDAYS_TOUCH_SELECT, KHDAYS_TOUCH_START, KHDAYS_TOUCH_GEAR};

static int open_now;
static int with_controls;              /* the on-screen controls are shown here: a touch screen's */
static int base_layout;                /* A or C: what its pieces started from, or KHDAYS_TOUCH_LAYOUT_CUSTOM */
static KhdaysLayoutEditor editor;
static KhdaysHudPlace hud[KHDAYS_HUD_GROUPS];
static KhdaysTouchLayout controls;
static KhdaysTouchScreen screen;
static float density_now;
static float ui_unit;                  /* the bar's and the names' point: readable on any window */
static SDL_FRect bar[BAR_BUTTONS];
static int picture_columns;
static int resizing;                   /* a finger on a control resizes it (Android's view) */

/* A group being dragged is not made in the picture again for each move, which took a weak chip 40 ms: it is lifted out
 * of one still picture, by comparing it with one made without the group, and drawn moving over that. */
enum {
    LIFT_NONE,
    LIFT_READ_WITH,    /* the next frame's picture has it */
    LIFT_HIDDEN,       /* the group is put out of sight: the picture is to be made again */
    LIFT_READ_WITHOUT, /* the next frame's picture does not have it */
    LIFT_DONE          /* the group moves on the screen alone; its texture, if any picture showed it */
};
#define LIFT_OUT_OF_SIGHT 4096.0f /* DS dots */
static struct {
    int stage;
    int item;
    SDL_Rect rect;       /* the window's part read, the item's whole with its room */
    KhdaysHudBox from;   /* the item's box when lifted */
    SDL_Surface *with;
    SDL_Texture *piece;
} lift;

static float dots_to_x(float dots)
{
    return controls.dots_x + dots * controls.dot;
}

static float dots_to_y(float dots)
{
    return controls.dots_y + dots * controls.dot;
}

static void group_box(int group, KhdaysHudBox *box)
{
    if (!khdays_hud_layout_visible(group, picture_columns, box)) {
        khdays_hud_layout_box_expected(group, picture_columns, screen.hud, box);
    }
}

/* Remakes the items from the places. */
static void items(void)
{
    editor.count = 0;
    khdays_hud_layout_set(hud);
    for (int g = 0; g < KHDAYS_HUD_GROUPS; ++g) {
        KhdaysHudBox b, whole;
        float scale;
        KhdaysEditorItem *it = &editor.item[editor.count++];
        group_box(g, &b);
        it->kind = ITEM_HUD;
        it->id = g;
        it->box.x0 = dots_to_x(b.x0);
        it->box.x1 = dots_to_x(b.x1);
        it->box.y0 = dots_to_y(b.y0);
        it->box.y1 = dots_to_y(b.y1);
        memset(&it->room, 0, sizeof(it->room));
        if (khdays_hud_layout_box(g, picture_columns, &whole, &scale)) {
            /* what the outline leaves out still stays on the screen */
            it->room.x0 = (b.x0 - whole.x0) * controls.dot;
            it->room.y0 = (b.y0 - whole.y0) * controls.dot;
            it->room.x1 = (whole.x1 - b.x1) * controls.dot;
            it->room.y1 = (whole.y1 - b.y1) * controls.dot;
        }
    }
    if (with_controls) {
        for (size_t c = 0; !khdays_touch_edit_can_resize() && c < sizeof(moved_controls) / sizeof(moved_controls[0]); ++c) {
            KhdaysEditorItem *it = &editor.item[editor.count++];
            const KhdaysTouchBox *b = &controls.box[moved_controls[c]];
            it->kind = ITEM_CONTROL;
            it->id = moved_controls[c];
            memset(&it->room, 0, sizeof(it->room));
            it->box.x0 = b->x0;
            it->box.y0 = b->y0;
            it->box.x1 = b->x1;
            it->box.y1 = b->y1;
        }
        khdays_touch_edit(&controls);
    }
}

static void read_screen(SDL_Renderer *renderer)
{
    const KhdaysProfile *p = khdays_devices_profile();
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(renderer, &w, &h); /* the window's whole: the current one is the letterboxed picture's */
    density_now = khdays_touch_density(renderer);
    ui_unit = (float)h / UI_ROWS > density_now ? (float)h / UI_ROWS : density_now;
    memset(&screen, 0, sizeof(screen));
    screen.w = (float)w;
    screen.h = (float)h;
    screen.density = density_now;
    screen.size = (float)p->touch.size / 100.0f;
    screen.columns = khdays_present_columns();
    screen.hud = khdays_composition_hud_factor();
    picture_columns = screen.columns;
}

/* The places of a preset, or of the player's own. */
static void load(int layout)
{
    const KhdaysProfile *p = khdays_devices_profile();
    base_layout = layout;
    if (layout == KHDAYS_TOUCH_LAYOUT_CUSTOM) {
        khdays_touch_layout_custom(&screen, p->hud_layout.custom ? p->hud_layout.place : NULL, p->touch.control, &controls);
    } else if (with_controls) {
        khdays_touch_layout_make(layout, &screen, &controls);
    } else {
        /* no controls to keep clear of: the composition's places */
        khdays_touch_layout_custom(&screen, NULL, p->touch.control, &controls);
    }
    memcpy(hud, controls.hud, sizeof(hud));
    items();
}

static void read_area(SDL_Renderer *renderer);

void khdays_layout_editor_open(SDL_Renderer *renderer)
{
    const KhdaysProfile *p = khdays_devices_profile();
    read_screen(renderer);
    with_controls = khdays_touch_available();
    memset(&editor, 0, sizeof(editor));
    editor.held = -1;
    editor.snap = 1;
    read_area(renderer);
    open_now = 1;
    load(with_controls ? p->touch.layout : KHDAYS_TOUCH_LAYOUT_CUSTOM);
}

int khdays_layout_editor_is_open(void)
{
    return open_now;
}

static void lift_begin(int index)
{
    const KhdaysEditorItem *it = &editor.item[index];
    KhdaysHudBox whole;
    float scale;
    if (it->kind != ITEM_HUD) {
        return;
    }
    lift.item = index;
    lift.from = it->box;
    if (!khdays_hud_layout_box(it->id, picture_columns, &whole, &scale)) {
        lift.stage = LIFT_DONE; /* no picture shows it: a stand-in, nothing to lift */
        return;
    }
    {
        const int x0 = (int)SDL_floorf(it->box.x0 - it->room.x0), y0 = (int)SDL_floorf(it->box.y0 - it->room.y0);
        const int x1 = (int)SDL_ceilf(it->box.x1 + it->room.x1), y1 = (int)SDL_ceilf(it->box.y1 + it->room.y1);
        lift.rect.x = x0 < 0 ? 0 : x0;
        lift.rect.y = y0 < 0 ? 0 : y0;
        lift.rect.w = (x1 > (int)screen.w ? (int)screen.w : x1) - lift.rect.x;
        lift.rect.h = (y1 > (int)screen.h ? (int)screen.h : y1) - lift.rect.y;
    }
    lift.stage = lift.rect.w > 0 && lift.rect.h > 0 ? LIFT_READ_WITH : LIFT_DONE;
}

/* Back in the pictures, where the places say. Returns whether it had been lifted. */
static int lift_end(void)
{
    const int lifted = lift.stage != LIFT_NONE;
    lift.stage = LIFT_NONE;
    SDL_DestroySurface(lift.with);
    SDL_DestroyTexture(lift.piece);
    lift.with = NULL;
    lift.piece = NULL;
    if (lifted) {
        khdays_hud_layout_set(hud);
    }
    return lifted;
}

static SDL_Surface *read_rgba(SDL_Renderer *renderer)
{
    SDL_Surface *read = SDL_RenderReadPixels(renderer, &lift.rect);
    SDL_Surface *rgba = read != NULL ? SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32) : NULL;
    SDL_DestroySurface(read);
    return rgba;
}

/* Reads this frame's picture for a lift, before anything of the editor's is over it. */
static void lift_read(SDL_Renderer *renderer)
{
    if (lift.stage == LIFT_READ_WITH) {
        KhdaysHudPlace hidden[KHDAYS_HUD_GROUPS];
        const int group = editor.item[lift.item].id;
        lift.with = read_rgba(renderer);
        if (lift.with == NULL) {
            lift.stage = LIFT_DONE; /* moves as an outline; where it ends is still right */
            return;
        }
        memcpy(hidden, hud, sizeof(hidden));
        hidden[group] = khdays_hud_place_moved(group, LIFT_OUT_OF_SIGHT, 0.0f);
        khdays_hud_layout_set(hidden);
        lift.stage = LIFT_HIDDEN;
    } else if (lift.stage == LIFT_READ_WITHOUT) {
        SDL_Surface *without = read_rgba(renderer);
        if (without != NULL && without->w == lift.with->w && without->h == lift.with->h) {
            khdays_editor_cutout((unsigned char *)lift.with->pixels, lift.with->pitch, (const unsigned char *)without->pixels,
                                 without->pitch, lift.with->w, lift.with->h);
            lift.piece = SDL_CreateTextureFromSurface(renderer, lift.with);
            if (lift.piece != NULL) {
                SDL_SetTextureBlendMode(lift.piece, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(lift.piece, SDL_SCALEMODE_NEAREST);
            }
        }
        SDL_DestroySurface(without);
        lift.stage = LIFT_DONE;
    }
}

void khdays_layout_editor_renderer_gone(void)
{
    SDL_DestroySurface(lift.with);
    SDL_DestroyTexture(lift.piece);
    lift.with = NULL;
    lift.piece = NULL;
    if (lift.stage != LIFT_NONE) {
        lift.stage = LIFT_DONE; /* an outline alone until it is let go */
    }
}

int khdays_layout_editor_wants_picture(void)
{
    if (open_now && lift.stage == LIFT_HIDDEN) {
        lift.stage = LIFT_READ_WITHOUT;
        return 1;
    }
    return 0;
}

/* What was dragged into the places; returns whether a HUD's group moved. */
static int take_drag(int index, const KhdaysHudBox *before)
{
    const KhdaysEditorItem *it = &editor.item[index];
    if (it->kind == ITEM_HUD) {
        const KhdaysHudBox from = {(before->x0 - controls.dots_x) / controls.dot, (before->y0 - controls.dots_y) / controls.dot,
                                   (before->x1 - controls.dots_x) / controls.dot, (before->y1 - controls.dots_y) / controls.dot};
        const KhdaysHudBox to = {(it->box.x0 - controls.dots_x) / controls.dot, (it->box.y0 - controls.dots_y) / controls.dot,
                                 (it->box.x1 - controls.dots_x) / controls.dot, (it->box.y1 - controls.dots_y) / controls.dot};
        hud[it->id] = khdays_editor_place(&hud[it->id], &from, &to, it->id, picture_columns);
        base_layout = KHDAYS_TOUCH_LAYOUT_CUSTOM;
        return 0; /* lifted: into the pictures when let go (lift_end) */
    }
    {
        KhdaysTouchBox *b = &controls.box[it->id];
        b->x0 = it->box.x0;
        b->y0 = it->box.y0;
        b->x1 = it->box.x1;
        b->y1 = it->box.y1;
        if (it->id == KHDAYS_TOUCH_STICK) {
            controls.box[KHDAYS_TOUCH_DPAD] = *b;
        }
        khdays_touch_edit(&controls);
        base_layout = KHDAYS_TOUCH_LAYOUT_CUSTOM;
    }
    return 0;
}

void khdays_layout_editor_control_box(int control, float x0, float y0, float x1, float y1)
{
    if (open_now && control >= 0 && control < KHDAYS_TOUCH_CONTROLS) {
        const KhdaysTouchBox box = {x0, y0, x1, y1};
        controls.box[control] = box;
        if (control == KHDAYS_TOUCH_STICK) {
            controls.box[KHDAYS_TOUCH_DPAD] = box;
        }
        khdays_touch_edit(&controls);
        base_layout = KHDAYS_TOUCH_LAYOUT_CUSTOM;
    }
}

/* The controls' places as they are now, in points from their nearest sides. */
static void control_places(KhdaysHudPlace *out)
{
    memset(out, 0, sizeof(KhdaysHudPlace) * KHDAYS_TOUCH_CONTROLS);
    for (size_t c = 0; c < sizeof(moved_controls) / sizeof(moved_controls[0]); ++c) {
        const int id = moved_controls[c];
        const KhdaysHudBox b = {controls.box[id].x0, controls.box[id].y0, controls.box[id].x1, controls.box[id].y1};
        out[id] = khdays_editor_control_place(&b, screen.w, screen.h, density_now);
    }
    out[KHDAYS_TOUCH_DPAD] = out[KHDAYS_TOUCH_STICK];
}

/* The window's own part an item may lie in, and the snap's lines. */
static void read_area(SDL_Renderer *renderer)
{
    SDL_Rect safe;
    SDL_Window *const window = SDL_GetRenderWindow(renderer);
    editor.grid = GRID_POINTS * density_now;
    editor.area.x0 = editor.area.y0 = 0.0f;
    editor.area.x1 = screen.w;
    editor.area.y1 = screen.h;
    if (SDL_GetWindowSafeArea(window, &safe) && safe.w > 0 && safe.h > 0) {
        const float k = SDL_GetWindowPixelDensity(window);
        editor.area.x0 = (float)safe.x * k;
        editor.area.y0 = (float)safe.y * k;
        editor.area.x1 = (float)(safe.x + safe.w) * k;
        editor.area.y1 = (float)(safe.y + safe.h) * k;
    }
}

/* The window changed shape with the editor open: what was placed so far, from its nearest sides, in the new one. */
static void window_changed(SDL_Renderer *renderer)
{
    KhdaysHudPlace kept[KHDAYS_TOUCH_CONTROLS];
    const int layout = base_layout;
    lift_end();
    control_places(kept);
    editor.held = -1;
    read_screen(renderer);
    read_area(renderer);
    khdays_touch_layout_custom(&screen, hud, kept, &controls);
    base_layout = layout;
    items();
}

static void finish(int keep, int *changed)
{
    lift_end();
    open_now = 0;
    resizing = 0;
    khdays_touch_edit_resizing(0);
    khdays_touch_edit(NULL);
    if (keep) {
        KhdaysProfile p = *khdays_devices_profile();
        if (!with_controls) {
            /* the HUD alone: the on-screen controls' settings are not this window's to change */
            memcpy(p.hud_layout.place, hud, sizeof(hud));
            p.hud_layout.custom = base_layout == KHDAYS_TOUCH_LAYOUT_CUSTOM;
        } else if (base_layout != KHDAYS_TOUCH_LAYOUT_CUSTOM) {
            p.touch.layout = base_layout; /* a preset as it is */
            p.hud_layout.custom = 0;
        } else {
            memcpy(p.hud_layout.place, hud, sizeof(hud));
            p.hud_layout.custom = 1;
            p.touch.layout = KHDAYS_TOUCH_LAYOUT_CUSTOM;
            control_places(p.touch.control);
        }
        khdays_devices_set_profile(&p);
        *changed = 1;
    } else {
        /* as they were */
        khdays_devices_set_profile(khdays_devices_profile());
    }
}

int khdays_layout_editor_event(const SDL_Event *event, SDL_Renderer *renderer, int *changed, int *moved)
{
    SDL_Window *const window = SDL_GetRenderWindow(renderer);
    const float k = SDL_GetWindowPixelDensity(window) > 0.0f ? SDL_GetWindowPixelDensity(window) : 1.0f;
    *changed = 0;
    *moved = 0;
    switch (event->type) {
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        window_changed(renderer);
        *moved = 1;
        return 0;
    case SDL_EVENT_KEY_DOWN:
        if (event->key.scancode == SDL_SCANCODE_ESCAPE || event->key.scancode == SDL_SCANCODE_AC_BACK) {
            finish(0, changed);
            *moved = 1;
            return 1;
        }
        return 0;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const float x = event->button.x * k, y = event->button.y * k;
        if (event->button.button != SDL_BUTTON_LEFT || editor.held >= 0) {
            return 0; /* (one piece at a time: another device's press changes nothing under the drag) */
        }
        for (int b = 0; b < BAR_BUTTONS; ++b) {
            const SDL_FPoint at = {x, y};
            if (SDL_PointInRectFloat(&at, &bar[b])) {
                switch (b) {
                case BAR_A: load(KHDAYS_TOUCH_LAYOUT_A); break;
                case BAR_C: load(KHDAYS_TOUCH_LAYOUT_C); break;
                case BAR_CUSTOM: load(KHDAYS_TOUCH_LAYOUT_CUSTOM); break;
                case BAR_SNAP: editor.snap = !editor.snap; break;
                case BAR_RESIZE:
                    resizing = !resizing;
                    khdays_touch_edit_resizing(resizing);
                    break;
                case BAR_RESET:
                    if (with_controls) {
                        load(KHDAYS_TOUCH_LAYOUT_C);
                    } else {
                        for (int g = 0; g < KHDAYS_HUD_GROUPS; ++g) {
                            hud[g] = khdays_hud_place_home(g);
                        }
                        items();
                        base_layout = KHDAYS_TOUCH_LAYOUT_A; /* none kept: the composition's own */
                    }
                    break;
                default: finish(1, changed); *moved = 1; return 1;
                }
                *moved = 1;
                return 0;
            }
        }
        if (khdays_editor_press(&editor, x, y) >= 0) {
            lift_begin(editor.held);
        }
        return 0;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (editor.held >= 0) {
            const int index = editor.held;
            const KhdaysHudBox before = editor.item[index].box;
            if (khdays_editor_drag(&editor, event->motion.x * k, event->motion.y * k)) {
                *moved = take_drag(index, &before);
            }
        }
        return 0;
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const int index = editor.held;
        const KhdaysHudBox before = index >= 0 ? editor.item[index].box : (KhdaysHudBox){0};
        if (khdays_editor_release(&editor)) {
            *moved = take_drag(index, &before);
        }
        *moved |= lift_end();
        return 0;
    }
    default: return 0;
    }
}

void khdays_layout_editor_done_at(SDL_Renderer *renderer, float *x, float *y)
{
    SDL_Window *const window = SDL_GetRenderWindow(renderer);
    const float k = SDL_GetWindowPixelDensity(window) > 0.0f ? SDL_GetWindowPixelDensity(window) : 1.0f;
    *x = (bar[BAR_DONE].x + bar[BAR_DONE].w / 2.0f) / k;
    *y = (bar[BAR_DONE].y + bar[BAR_DONE].h / 2.0f) / k;
}

/* ---- Drawing ------------------------------------------------------------- */

static void outline(SDL_Renderer *renderer, const KhdaysHudBox *b, SDL_FColor c)
{
    const float dash = DASH_POINTS * ui_unit, line = LINE_POINTS * ui_unit;
    SDL_SetRenderDrawColorFloat(renderer, c.r, c.g, c.b, c.a);
    for (float x = b->x0; x < b->x1; x += dash * 2.0f) {
        const float w = x + dash > b->x1 ? b->x1 - x : dash;
        const SDL_FRect top = {x, b->y0, w, line}, foot = {x, b->y1 - line, w, line};
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &foot);
    }
    for (float y = b->y0; y < b->y1; y += dash * 2.0f) {
        const float h = y + dash > b->y1 ? b->y1 - y : dash;
        const SDL_FRect left = {b->x0, y, line, h}, right = {b->x1 - line, y, line, h};
        SDL_RenderFillRect(renderer, &left);
        SDL_RenderFillRect(renderer, &right);
    }
}

static void centred(SDL_Renderer *renderer, float cx, float cy, float height, SDL_Color c, const char *s)
{
    int w = 0, h = 0;
    SDL_Texture *t = khdays_ui_font_load() ? khdays_ui_font_text(renderer, s, &w, &h) : NULL;
    if (t != NULL && h > 0) {
        const float k = height / (float)h;
        const SDL_FRect dst = {cx - (float)w * k / 2.0f, cy - height / 2.0f, (float)w * k, height};
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureColorMod(t, c.r, c.g, c.b);
        SDL_RenderTexture(renderer, t, NULL, &dst);
        SDL_SetTextureColorMod(t, 255, 255, 255);
    }
}

static void draw_bar(SDL_Renderer *renderer)
{
    static const int words[BAR_BUTTONS] = {TXT_TOUCH_LAYOUT_A, TXT_TOUCH_LAYOUT_C, TXT_TOUCH_LAYOUT_CUSTOM, TXT_LAYOUT_SNAP,
                                           TXT_LAYOUT_RESIZE,  TXT_LAYOUT_RESET,   TXT_LAYOUT_DONE};
    const float h = BAR_H_POINTS * ui_unit, gap = BAR_GAP_POINTS * ui_unit, pad = BAR_PAD_POINTS * ui_unit;
    const float inner = h - 2.0f * gap, text = inner * LABEL_OF_BAR;
    float widths[BAR_BUTTONS], total = gap;
    const char *names[BAR_BUTTONS];
    for (int b = 0; b < BAR_BUTTONS; ++b) {
        int w = 0, th = 0;
        const int shown = b == BAR_RESIZE ? with_controls && khdays_touch_edit_can_resize()
                                          : with_controls || (b != BAR_A && b != BAR_C && b != BAR_CUSTOM);
        names[b] = khdays_text(words[b]);
        widths[b] = 0.0f;
        if (shown && khdays_ui_font_load() && khdays_ui_font_text(renderer, names[b], &w, &th) != NULL && th > 0) {
            widths[b] = (float)w * text / (float)th + 2.0f * pad;
            total += widths[b] + gap;
        }
    }
    {
        float x = (screen.w - total) / 2.0f;
        /* where it covers the fewest of the pieces */
        static const float downs[3] = {BAR_DOWN, 0.5f, 0.72f};
        float y = screen.h * downs[0];
        int fewest = editor.count + 1;
        for (int d = 0; d < 3; ++d) {
            const float top = screen.h * downs[d];
            int covered = 0;
            for (int i = 0; i < editor.count; ++i) {
                const KhdaysHudBox *b = &editor.item[i].box;
                covered += b->x0 < x + total && b->x1 > x && b->y0 < top + h && b->y1 > top;
            }
            for (size_t c = 0; with_controls && c < sizeof(moved_controls) / sizeof(moved_controls[0]); ++c) {
                const KhdaysTouchBox *b = &controls.box[moved_controls[c]];
                covered += b->x0 < x + total && b->x1 > x && b->y0 < top + h && b->y1 > top; /* the view would take the touch */
            }
            if (covered < fewest) {
                fewest = covered;
                y = top;
            }
        }
        const SDL_FRect back = {x, y, total, h};
        SDL_SetRenderDrawColor(renderer, 25, 25, 32, 240);
        SDL_RenderFillRect(renderer, &back);
        x += gap;
        for (int b = 0; b < BAR_BUTTONS; ++b) {
            const int lit = (b == BAR_SNAP && editor.snap) || (b == BAR_RESIZE && resizing) || (b == BAR_A && base_layout == KHDAYS_TOUCH_LAYOUT_A) ||
                            (b == BAR_C && base_layout == KHDAYS_TOUCH_LAYOUT_C) ||
                            (b == BAR_CUSTOM && base_layout == KHDAYS_TOUCH_LAYOUT_CUSTOM);
            SDL_FRect r = {x, y + gap, widths[b], inner};
            if (widths[b] <= 0.0f) {
                bar[b].w = bar[b].h = 0.0f;
                continue;
            }
            bar[b] = r;
            if (lit) {
                SDL_SetRenderDrawColor(renderer, 90, 220, 255, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 70, 70, 80, 255);
            }
            SDL_RenderFillRect(renderer, &r);
            centred(renderer, x + widths[b] / 2.0f, y + h / 2.0f, text,
                    lit ? (SDL_Color){10, 10, 10, 255} : (SDL_Color){255, 255, 255, 255}, names[b]);
            x += widths[b] + gap;
        }
    }
}

void khdays_layout_editor_draw(SDL_Renderer *renderer)
{
    static const int group_words[KHDAYS_HUD_GROUPS] = {TXT_HUD_GROUP_MISSION,   TXT_HUD_GROUP_COUNTER, TXT_HUD_GROUP_MAP,
                                                       TXT_HUD_GROUP_COUNTDOWN, TXT_HUD_GROUP_DECK,    TXT_HUD_GROUP_PARTY};
    SDL_RendererLogicalPresentation mode;
    SDL_BlendMode blend;
    int lw = 0, lh = 0;
    const SDL_FColor cyan = {0.35f, 0.86f, 1.0f, 1.0f}, yellow = {1.0f, 0.82f, 0.24f, 1.0f};
    if (!open_now) {
        return;
    }
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_GetRenderDrawBlendMode(renderer, &blend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    lift_read(renderer);
    if (lift.stage == LIFT_DONE && lift.piece != NULL) {
        const KhdaysHudBox *b = &editor.item[lift.item].box;
        const SDL_FRect at = {(float)lift.rect.x + b->x0 - lift.from.x0, (float)lift.rect.y + b->y0 - lift.from.y0,
                              (float)lift.rect.w, (float)lift.rect.h};
        SDL_RenderTexture(renderer, lift.piece, NULL, &at);
    }
    if (editor.snap) {
        const float dot = density_now > 1.0f ? density_now : 1.0f;
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 90);
        for (float y = editor.grid; y < screen.h; y += editor.grid) {
            for (float x = editor.grid; x < screen.w; x += editor.grid) {
                const SDL_FRect r = {x - dot / 2.0f, y - dot / 2.0f, dot, dot};
                SDL_RenderFillRect(renderer, &r);
            }
        }
    }
    for (int i = 0; i < editor.count; ++i) {
        const KhdaysEditorItem *it = &editor.item[i];
        KhdaysHudBox unused;
        float scale;
        if (it->kind == ITEM_HUD && !khdays_hud_layout_box(it->id, picture_columns, &unused, &scale)) {
            /* no picture has shown it yet: a stand-in where it will be */
            const SDL_FRect r = {it->box.x0, it->box.y0, it->box.x1 - it->box.x0, it->box.y1 - it->box.y0};
            SDL_SetRenderDrawColor(renderer, 90, 220, 255, 60);
            SDL_RenderFillRect(renderer, &r);
        }
        outline(renderer, &it->box, i == editor.held ? yellow : cyan);
        if (it->kind == ITEM_HUD) {
            centred(renderer, (it->box.x0 + it->box.x1) / 2.0f, (it->box.y0 + it->box.y1) / 2.0f, NAME_POINTS * ui_unit,
                    (SDL_Color){255, 255, 255, 255}, khdays_text(group_words[it->id]));
        }
    }
    if (editor.held < 0) {
        draw_bar(renderer); /* out of the way while something is dragged */
    }
    SDL_SetRenderDrawBlendMode(renderer, blend);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
}
