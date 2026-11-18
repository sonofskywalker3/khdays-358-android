/* first_start_notice.h: the words shown while the graphics card's programs are made the first time.
 *
 * Drawn with the settings menu's font (Roboto Medium, in the executable) through stb_truetype, the copy
 * Dear ImGui carries (imstb_truetype.h, MIT / public domain), compiled here as its own static copy. */
#include "first_start_notice.h"

#include "../screens/ui_text.h"

#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "imstb_truetype.h"

#include "menu_font.h"

/* sizes in the window's height: the title and the line under it (the mockup Jeff saw at 1080: 64 and 40) */
#define TITLE_SHARE (64.0f / 1080.0f)
#define LINE_SHARE (40.0f / 1080.0f)
#define GAP_SHARE (26.0f / 1080.0f)
#define WIDTH_SHARE 0.9f /* the most of the window's width a line takes */
#define LINE_GREY 170

/* The next code point of UTF-8 text (malformed bytes taken as themselves). */
static int next_code(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int c = p[0], n = 0;
    if (c >= 0xf0) {
        c &= 0x07, n = 3;
    } else if (c >= 0xe0) {
        c &= 0x0f, n = 2;
    } else if (c >= 0xc0) {
        c &= 0x1f, n = 1;
    }
    ++p;
    for (int i = 0; i < n && (*p & 0xc0) == 0x80; ++i, ++p) {
        c = (c << 6) | (*p & 0x3f);
    }
    *s = (const char *)p;
    return c;
}

/* How wide `text` is at `px` pixels tall. */
static float text_width(const stbtt_fontinfo *font, const char *text, float px)
{
    const float scale = stbtt_ScaleForPixelHeight(font, px);
    float width = 0.0f;
    for (const char *s = text; *s != '\0';) {
        int advance, bearing;
        stbtt_GetCodepointHMetrics(font, next_code(&s), &advance, &bearing);
        width += SDL_ceilf((float)advance * scale);
    }
    return width + 2.0f;
}

/* `text` at `px` pixels tall as a white texture with the coverage in its alpha; its size in *w, *h. */
static SDL_Texture *text_texture(SDL_Renderer *r, const stbtt_fontinfo *font, const char *text, float px, int *w,
                                 int *h)
{
    const float scale = stbtt_ScaleForPixelHeight(font, px);
    int ascent, descent, gap, width = 0;
    stbtt_GetFontVMetrics(font, &ascent, &descent, &gap);
    const int baseline = (int)SDL_ceilf((float)ascent * scale);
    const int height = baseline + (int)SDL_ceilf((float)-descent * scale) + 1;
    for (const char *s = text; *s != '\0';) {
        int advance, bearing;
        stbtt_GetCodepointHMetrics(font, next_code(&s), &advance, &bearing);
        width += (int)SDL_ceilf((float)advance * scale);
    }
    width += 2;
    if (width <= 2 || height <= 1) {
        return NULL;
    }
    unsigned char *coverage = (unsigned char *)SDL_calloc((size_t)width * (size_t)height, 1);
    Uint32 *pixels = (Uint32 *)SDL_malloc((size_t)width * (size_t)height * 4u);
    SDL_Texture *t = NULL;
    if (coverage != NULL && pixels != NULL) {
        float x = 1.0f;
        for (const char *s = text; *s != '\0';) {
            const int code = next_code(&s);
            int advance, bearing, x0, y0, x1, y1;
            stbtt_GetCodepointHMetrics(font, code, &advance, &bearing);
            stbtt_GetCodepointBitmapBoxSubpixel(font, code, scale, scale, x - SDL_floorf(x), 0.0f, &x0, &y0, &x1, &y1);
            const int gx = (int)SDL_floorf(x) + x0, gy = baseline + y0;
            if (gx >= 0 && gy >= 0 && gx + (x1 - x0) <= width && gy + (y1 - y0) <= height) {
                /* glyphs touch rarely and only by their edges: the larger coverage wins */
                static unsigned char glyph[256 * 256]; /* (off the stack: Android's threads have little) */
                const int gw = x1 - x0, gh = y1 - y0;
                if (gw > 0 && gh > 0 && gw <= 256 && gh <= 256) {
                    stbtt_MakeCodepointBitmapSubpixel(font, glyph, gw, gh, gw, scale, scale, x - SDL_floorf(x), 0.0f,
                                                      code);
                    for (int yy = 0; yy < gh; ++yy) {
                        for (int xx = 0; xx < gw; ++xx) {
                            unsigned char *out = coverage + (size_t)(gy + yy) * (size_t)width + (size_t)(gx + xx);
                            const unsigned char v = glyph[yy * gw + xx];
                            *out = v > *out ? v : *out;
                        }
                    }
                }
            }
            x += (float)advance * scale;
        }
        for (int i = 0; i < width * height; ++i) {
            pixels[i] = ((Uint32)coverage[i] << 24) | 0x00ffffffu;
        }
        t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
        if (t != NULL) {
            SDL_UpdateTexture(t, NULL, pixels, width * 4);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
            *w = width, *h = height;
        }
    }
    SDL_free(coverage);
    SDL_free(pixels);
    return t;
}

void khdays_first_start_notice(SDL_Renderer *r)
{
    stbtt_fontinfo font;
    int ow = 0, oh = 0, tw = 0, th = 0, lw = 0, lh = 0;
    if (r == NULL || !SDL_GetCurrentRenderOutputSize(r, &ow, &oh) || ow <= 0 || oh <= 0 ||
        !stbtt_InitFont(&font, khdays_menu_font, stbtt_GetFontOffsetForIndex(khdays_menu_font, 0))) {
        return;
    }
    const char *title_text = khdays_text(TXT_FIRST_START_TITLE), *line_text = khdays_text(TXT_FIRST_START_LINE);
    /* sized by the height, smaller where a line would not fit the width (a tall window, a long language) */
    float fit = 1.0f;
    const float room = WIDTH_SHARE * (float)ow;
    const float widest = SDL_max(text_width(&font, title_text, TITLE_SHARE * (float)oh),
                                 text_width(&font, line_text, LINE_SHARE * (float)oh));
    if (widest > room && widest > 0.0f) {
        fit = room / widest;
    }
    SDL_Texture *title = text_texture(r, &font, title_text, fit * TITLE_SHARE * (float)oh, &tw, &th);
    SDL_Texture *line = text_texture(r, &font, line_text, fit * LINE_SHARE * (float)oh, &lw, &lh);
    const float gap = fit * GAP_SHARE * (float)oh;
    const float top = ((float)oh - (float)th - gap - (float)lh) / 2.0f;
    /* twice: the window's swap chain may hold two pictures, and the next one is the game's own */
    for (int i = 0; i < 2; ++i) {
        SDL_SetRenderViewport(r, NULL);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        if (title != NULL) {
            const SDL_FRect at = {SDL_floorf(((float)ow - (float)tw) / 2.0f), SDL_floorf(top), (float)tw, (float)th};
            SDL_SetTextureColorMod(title, 255, 255, 255);
            SDL_RenderTexture(r, title, NULL, &at);
        }
        if (line != NULL) {
            const SDL_FRect at = {SDL_floorf(((float)ow - (float)lw) / 2.0f), SDL_floorf(top + (float)th + gap),
                                  (float)lw, (float)lh};
            SDL_SetTextureColorMod(line, LINE_GREY, LINE_GREY, LINE_GREY);
            SDL_RenderTexture(r, line, NULL, &at);
        }
        SDL_RenderPresent(r);
    }
    SDL_DestroyTexture(title);
    SDL_DestroyTexture(line);
}
