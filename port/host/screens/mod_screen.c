/* A mod's screens and the title's mods' prompt (mod_screen.h): in the mod's own layout when its screen
 * names one (layouts/<name>.txt, docs/HD_SCREENS.md), else in the HD PACK's room list layout, else
 * plainly in the game's own font over a veil. docs/SCRIPTING.md lists the pieces a layout gives. */
#include "mod_screen.h"

#include "layout_screen.h"
#include "ui_font.h"
#include "ui_text.h"
#include "../../runtime/scripts/screens.h"

#include <stdlib.h>
#include <string.h>

#define PAGE_LINE 44.0f /* a page's lines apart, in the picture's texels */
#define GLYPH_A 0x3349u /* the game's characters for its buttons (layout_screen.c) */
#define GLYPH_B 0x3314u

static const Uint8 WHITE[3] = {255, 255, 255};
static const Uint8 BLACK[3] = {0, 0, 0};
static const Uint8 RED[3] = {170, 32, 40}; /* the plain look's chosen row, the camp menu's red */

static struct {
    KhdaysLayout layout;
    SDL_Texture *art;
    float rows[2]; /* the first row's top, the rows apart */
    int rows_shown;
    int ok;               /* the layout read */
    char path[1024];      /* the layout's file tried */
    SDL_Texture *picture; /* a page's */
    char picture_path[1024];
    int pw, ph;
    int first; /* a list's first row shown */
} s;

static void forget_layout(void)
{
    if (s.art != NULL) {
        SDL_DestroyTexture(s.art);
        s.art = NULL;
    }
    khdays_layout_forget(&s.layout);
    s.ok = 0;
    s.rows_shown = 0;
}

static void forget_picture(void)
{
    if (s.picture != NULL) {
        SDL_DestroyTexture(s.picture);
        s.picture = NULL;
    }
    s.picture_path[0] = '\0';
}

void khdays_mod_screen_renderer_gone(void)
{
    forget_layout();
    forget_picture();
    s.path[0] = '\0';
}

static int load_layout(SDL_Renderer *renderer, const char *path)
{
    char folder[1024], file[256], *text, *next, *slash;
    size_t size = 0;
    forget_layout();
    s.layout.who = "a mod's screen";
    s.layout.renderer = renderer;
    if ((text = (char *)SDL_LoadFile(path, &size)) == NULL) {
        return 0;
    }
    SDL_snprintf(folder, sizeof(folder), "%s", path);
    slash = strrchr(folder, '/');
    if (strrchr(folder, '\\') > slash) {
        slash = strrchr(folder, '\\');
    }
    if (slash != NULL) {
        slash[1] = '\0';
    }
    for (char *line = text; line != NULL && *line != '\0'; line = next) {
        float a, b;
        int n;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if (khdays_layout_line(&s.layout, line, folder)) {
            continue;
        }
        if (SDL_sscanf(line, "rows %f %f %d", &a, &b, &n) == 3) {
            s.rows[0] = a;
            s.rows[1] = b;
            s.rows_shown = n > 0 ? n : 1;
        } else if (SDL_sscanf(line, "art %255s", file) == 1) {
            char full[1300];
            int w, h;
            SDL_snprintf(full, sizeof(full), "%s%s", folder, file);
            s.art = khdays_layout_texture(&s.layout, full, 0, &w, &h);
        }
    }
    SDL_free(text);
    if (!khdays_layout_ready(&s.layout) || s.rows_shown == 0) {
        SDL_Log("mod screen: %s is incomplete", path);
        forget_layout();
        return 0;
    }
    return 1;
}

/* The layout for a screen: the mod's, else the HD theater's list; read again when it changes. */
static void choose_layout(SDL_Renderer *renderer, const char *mods_layout, int language)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    char path[1024] = "";
    if (mods_layout != NULL) {
        SDL_snprintf(path, sizeof(path), "%s", mods_layout);
    } else {
        char relative[96];
        SDL_snprintf(relative, sizeof(relative), "layouts/rooms/%s/rooms.txt",
                     codes[language >= 1 && language <= 5 ? language : 1]);
        if (!khdays_mods_find(relative, path, (int)sizeof(path))) {
            path[0] = '\0';
        }
    }
    if (strcmp(path, s.path) == 0 && renderer == s.layout.renderer) {
        return;
    }
    SDL_snprintf(s.path, sizeof(s.path), "%s", path);
    s.ok = path[0] != '\0' && load_layout(renderer, path);
}

/* ---- In a layout --------------------------------------------------------------------------- */

static unsigned short *wide(const char *utf8)
{
    static unsigned short *buffer;
    static size_t room;
    const size_t want = strlen(utf8) + 1;
    if (want > room) {
        unsigned short *more = (unsigned short *)realloc(buffer, want * sizeof(*buffer));
        if (more == NULL) {
            return NULL;
        }
        buffer = more;
        room = want;
    }
    khdays_layout_utf8(utf8, buffer, (int)want);
    return buffer;
}

static void text(const KhdaysLayoutFrame *fr, const char *style, const char *utf8, float dx, float dy)
{
    const unsigned short *w = utf8 != NULL ? wide(utf8) : NULL;
    if (w != NULL) {
        khdays_layout_draw_text(&s.layout, fr, style, w, dx, dy, 1.0f);
    }
}

static const float *place(const char *name)
{
    static const float none[4] = {0, 0, 0, 0};
    const KhdaysLayoutPlace *p = khdays_layout_place(&s.layout, name);
    return p != NULL ? &p->x : none;
}

/* A line of buttons and their words, each button the game's character for it (drawn as the player's
 * input's icon by the layout): "<A> word   <B> word". */
static unsigned short *buttons_line(unsigned short first, const char *first_word, unsigned short second,
                                    const char *second_word)
{
    static unsigned short line[256];
    int n = 0;
    const unsigned short buttons[2] = {first, second};
    const char *words[2] = {first_word, second_word};
    for (int b = 0; b < 2 && buttons[b] != 0; ++b) {
        const unsigned short *w = wide(words[b]);
        if (b > 0) {
            for (int k = 0; k < 4 && n < 250; ++k) {
                line[n++] = ' ';
            }
        }
        line[n++] = buttons[b];
        line[n++] = ' ';
        for (; w != NULL && *w != 0 && n < 254; ++w) {
            line[n++] = *w;
        }
    }
    line[n] = 0;
    return line;
}

static void draw_rows(const KhdaysLayoutFrame *fr, const KhdaysScreen *scr)
{
    const int list = scr->kind == KHDAYS_SCREEN_LIST, shown = s.rows_shown;
    if (list) {
        if (scr->cursor < s.first) {
            s.first = scr->cursor;
        } else if (scr->cursor >= s.first + shown) {
            s.first = scr->cursor - shown + 1;
        }
    } else {
        s.first = 0;
    }
    for (int k = 0; k < shown && s.first + k < scr->row_count; ++k) {
        const int i = s.first + k, chosen = list && i == scr->cursor;
        const float y = s.rows[0] + (float)k * s.rows[1];
        khdays_layout_draw_placed(&s.layout, fr, chosen ? "row_selected" : "row", 0, y, 1.0f);
        khdays_layout_draw_placed(&s.layout, fr, "square", 0, y, 1.0f);
        if (chosen) {
            khdays_layout_draw_placed(&s.layout, fr, "glove", 0, y, 1.0f);
        }
        text(fr, "single", scr->rows[i].label, 0, y);
        if (scr->rows[i].value != NULL) {
            text(fr, "players", scr->rows[i].value, 0, y);
        }
    }
    if (list && scr->row_count > shown) {
        const float *trough = place("trough"), *thumb = place("thumb");
        const float travel = trough[3] - thumb[3];
        const float t = (float)s.first / (float)(scr->row_count - shown);
        khdays_layout_draw_placed(&s.layout, fr, "trough", 0, 0, 1.0f);
        khdays_layout_draw_piece(&s.layout, fr, "thumb", thumb[0], trough[1] + travel * t, thumb[2], thumb[3], 1.0f);
    }
}

static void draw_picture(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, const KhdaysScreen *scr)
{
    float w, h, scale;
    SDL_FRect at;
    if (scr->picture == NULL) {
        return;
    }
    if (strcmp(scr->picture, s.picture_path) != 0) {
        forget_picture();
        SDL_snprintf(s.picture_path, sizeof(s.picture_path), "%s", scr->picture);
        s.picture = khdays_layout_texture(&s.layout, scr->picture, 0, &s.pw, &s.ph);
    }
    if (s.picture == NULL || s.pw <= 0 || s.ph <= 0) {
        return;
    }
    /* on the right half, under the heading, its whole fitted */
    scale = SDL_min(700.0f / (float)s.pw, 640.0f / (float)s.ph);
    w = (float)s.pw * scale;
    h = (float)s.ph * scale;
    at = khdays_layout_to_window(fr, 1840.0f - w, 220.0f, w, h);
    SDL_RenderTexture(renderer, s.picture, NULL, &at);
}

static void draw_layout_screen(SDL_Renderer *renderer, const KhdaysScreen *scr)
{
    KhdaysLayoutFrame fr;
    khdays_layout_begin(renderer, &fr);
    if (s.art != NULL) {
        const SDL_FRect all = khdays_layout_to_window(&fr, 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H);
        SDL_RenderTexture(renderer, s.art, NULL, &all);
    }
    khdays_layout_draw_piece(&s.layout, &fr, "veil", 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H, 1.0f);
    {
        const float *rule = place("rule");
        if (rule[2] > 0) {
            khdays_layout_fill_rounded(renderer, &fr, rule[0], rule[1], rule[2], rule[3], 0, WHITE, 0.47f);
        }
    }
    khdays_layout_draw_placed(&s.layout, &fr, "help_bar", 0, 0, 1.0f);
    text(&fr, "heading", scr->title, 0, 0);
    if (scr->kind == KHDAYS_SCREEN_PAGE) {
        char *copy = scr->text != NULL ? SDL_strdup(scr->text) : NULL;
        float dy = 0;
        for (char *line = copy, *next; line != NULL; line = next, dy += PAGE_LINE) {
            next = strchr(line, '\n');
            if (next != NULL) {
                *next++ = '\0';
            }
            text(&fr, "message", line, 0, dy);
        }
        SDL_free(copy);
        draw_picture(renderer, &fr, scr);
    } else {
        draw_rows(&fr, scr);
    }
    khdays_layout_draw_text(&s.layout, &fr, "help",
                            scr->kind == KHDAYS_SCREEN_LIST
                                ? buttons_line(GLYPH_A, khdays_text(TXT_MOD_CHOOSE), GLYPH_B, khdays_text(TXT_BACK))
                                : buttons_line(GLYPH_A, khdays_text(TXT_CLOSE), 0, NULL),
                            0, 0, 1.0f);
    khdays_layout_end(renderer, &fr);
}

/* The title's prompt in its bottom right corner (the settings key is in its top right): the player's
 * input for Y, the entry's label. */
static void draw_layout_prompt(SDL_Renderer *renderer, const char *label)
{
    KhdaysLayoutFrame fr;
    const unsigned short *line = buttons_line(KHDAYS_LAYOUT_GLYPH_Y, label, 0, NULL);
    const KhdaysLayoutText *single = khdays_layout_text(&s.layout, "single");
    const float size = single != NULL ? single->size : 1.0f, right = 1840.0f, y = 990.0f;
    const float width = khdays_layout_line_width(&s.layout, line, size), x = right - width;
    khdays_layout_begin_over(renderer, &fr);
    khdays_layout_fill_rounded(renderer, &fr, x - 30.0f, y - 22.0f, width + 60.0f, 74.0f, 22.0f, BLACK, 0.6f);
    khdays_layout_draw_line(&s.layout, &fr, line, x, y, size, 255.0f, 1.0f, 1); /* its shadow */
    khdays_layout_draw_line(&s.layout, &fr, line, x, y, size, 255.0f, 1.0f, 0);
    khdays_layout_end(renderer, &fr);
}

/* ---- Plainly, in the game's font ------------------------------------------------------------ */
/* In the layouts' 1920 x 1080 picture fitted to the window, over the game's picture dimmed. */

#define PLAIN_TEXT 44.0f /* a line's height */
#define PLAIN_LEFT 160.0f
#define PLAIN_RIGHT 1760.0f
#define PLAIN_ROWS 300.0f /* the first row's top */
#define PLAIN_PITCH 72.0f
#define PLAIN_SHOWN 9

/* A text's width in the picture at `height`; draws it there unless `measure`. */
static float plain_text(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, const char *utf8, float x, float y,
                        float height, int right, int measure)
{
    int w = 0, h = 0;
    SDL_Texture *t = utf8 != NULL && *utf8 != '\0' ? khdays_ui_font_text(renderer, utf8, &w, &h) : NULL;
    float scale, width;
    if (t == NULL || h <= 0) {
        return 0;
    }
    scale = height / (float)h;
    width = (float)w * scale;
    if (!measure) {
        const SDL_FRect at = khdays_layout_to_window(fr, right ? x - width : x, y, width, height);
        SDL_RenderTexture(renderer, t, NULL, &at);
    }
    return width;
}

static void plain_fill(SDL_Renderer *renderer, const KhdaysLayoutFrame *fr, float x, float y, float w, float h,
                       const Uint8 c[3], Uint8 alpha)
{
    const SDL_FRect r = khdays_layout_to_window(fr, x, y, w, h);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, c[0], c[1], c[2], alpha);
    SDL_RenderFillRect(renderer, &r);
}

static void draw_plain_screen(SDL_Renderer *renderer, const KhdaysScreen *scr)
{
    KhdaysLayoutFrame fr;
    char help[160];
    float y = PLAIN_ROWS;
    khdays_ui_font_load();
    khdays_layout_begin_over(renderer, &fr);
    plain_fill(renderer, &fr, 0, 0, KHDAYS_LAYOUT_W, KHDAYS_LAYOUT_H, BLACK, 215);
    plain_text(renderer, &fr, scr->title, PLAIN_LEFT, 110.0f, PLAIN_TEXT * 1.5f, 0, 0);
    plain_fill(renderer, &fr, PLAIN_LEFT, 200.0f, PLAIN_RIGHT - PLAIN_LEFT, 2, WHITE, 120);
    if (scr->kind == KHDAYS_SCREEN_PAGE) {
        char *copy = scr->text != NULL ? SDL_strdup(scr->text) : NULL;
        for (char *l = copy, *next; l != NULL; l = next, y += PLAIN_PITCH) {
            next = strchr(l, '\n');
            if (next != NULL) {
                *next++ = '\0';
            }
            plain_text(renderer, &fr, l, PLAIN_LEFT, y, PLAIN_TEXT, 0, 0);
        }
        SDL_free(copy);
    } else {
        const int list = scr->kind == KHDAYS_SCREEN_LIST;
        if (list && scr->cursor < s.first) {
            s.first = scr->cursor;
        } else if (list && scr->cursor >= s.first + PLAIN_SHOWN) {
            s.first = scr->cursor - PLAIN_SHOWN + 1;
        } else if (!list) {
            s.first = 0;
        }
        for (int k = 0; k < PLAIN_SHOWN && s.first + k < scr->row_count; ++k, y += PLAIN_PITCH) {
            const int i = s.first + k;
            if (list && i == scr->cursor) {
                plain_fill(renderer, &fr, PLAIN_LEFT - 30.0f, y - 14.0f, PLAIN_RIGHT - PLAIN_LEFT + 60.0f,
                           PLAIN_PITCH - 4.0f, RED, 230);
            }
            plain_text(renderer, &fr, scr->rows[i].label, PLAIN_LEFT, y, PLAIN_TEXT, 0, 0);
            if (scr->rows[i].value != NULL) {
                plain_text(renderer, &fr, scr->rows[i].value, PLAIN_RIGHT, y, PLAIN_TEXT, 1, 0);
            }
        }
    }
    if (scr->kind == KHDAYS_SCREEN_LIST) {
        SDL_snprintf(help, sizeof(help), "A  %s      B  %s", khdays_text(TXT_MOD_CHOOSE), khdays_text(TXT_BACK));
    } else {
        SDL_snprintf(help, sizeof(help), "A  %s", khdays_text(TXT_CLOSE));
    }
    plain_text(renderer, &fr, help, PLAIN_LEFT, 980.0f, PLAIN_TEXT * 0.8f, 0, 0);
    khdays_layout_end(renderer, &fr);
}

static void draw_plain_prompt(SDL_Renderer *renderer, const char *label)
{
    KhdaysLayoutFrame fr;
    char line[300];
    float width;
    khdays_ui_font_load();
    SDL_snprintf(line, sizeof(line), "Y  %s", label);
    khdays_layout_begin_over(renderer, &fr);
    width = plain_text(renderer, &fr, line, 0, 0, PLAIN_TEXT, 0, 1);
    plain_fill(renderer, &fr, 1840.0f - width - 30.0f, 968.0f, width + 60.0f, PLAIN_TEXT + 36.0f, BLACK, 160);
    plain_text(renderer, &fr, line, 1840.0f, 986.0f, PLAIN_TEXT, 1, 0);
    khdays_layout_end(renderer, &fr);
}

/* ---- The window's ----------------------------------------------------------------------------- */

int khdays_mod_screen_draw(SDL_Renderer *renderer)
{
    const KhdaysScreen *scr;
    const char *prompt;
    khdays_screens_lock();
    scr = khdays_screens_current();
    prompt = khdays_screens_prompt();
    if (scr->kind == KHDAYS_SCREEN_NONE && prompt == NULL) {
        khdays_screens_unlock();
        return 0;
    }
    choose_layout(renderer, scr->kind != KHDAYS_SCREEN_NONE ? scr->layout : NULL, khdays_screens_language());
    if (scr->kind != KHDAYS_SCREEN_NONE) {
        if (s.ok) {
            draw_layout_screen(renderer, scr);
        } else {
            draw_plain_screen(renderer, scr);
        }
    } else {
        const char *label = prompt[0] != '\0' ? prompt : khdays_text(TXT_MODS);
        if (s.ok) {
            draw_layout_prompt(renderer, label);
        } else {
            draw_plain_prompt(renderer, label);
        }
    }
    khdays_screens_unlock();
    return 1;
}
