/* The port's text over the finished one-screen picture (text_overlay.h). */
#include "text_overlay.h"

#include "../../gfx/elements.h"
#include "../../gfx/frame.h"
#include "../../gfx/onescreen.h"
#include "../../runtime/display/frame_package.h"
#include "../../runtime/mods/textures.h"
#include "../../ui/situation.h"

#include <string.h>

#define PIECES_MAX 96
#define TEXTURES_MAX 24
#define SCALE_MAX 6    /* the images' scale at most: a 256 x 192 canvas is then 7 MB */
#define COARSER_BY 2   /* drawn apart where the window shows the 2D at this many times its size or more */
#define UNUSED_DRAWS 8 /* a texture no piece asked for in so many draws is let go */

typedef struct Texture {
    SDL_Texture *texture;
    uint32_t image, version;
    int w, h, unused; /* `unused`: draws since a piece asked for it */
    int read;         /* its texels were read once */
} Texture;

static KhdaysTextPiece pieces[PIECES_MAX];
static int piece_count;
static int fresh; /* pieces taken since the last draw: their images are read again then, not after */
static Texture textures[TEXTURES_MAX];
static SDL_Renderer *textures_of;

static int enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = SDL_getenv("KHDAYS_TEXT_OVERLAY");
        on = !(e != NULL && e[0] == '0');
    }
    return on;
}

int khdays_text_overlay_scale(int scale2d, float shown, int scene, unsigned states)
{
    int n = (int)SDL_ceilf(shown - 0.01f);
    n = n > SCALE_MAX ? SCALE_MAX : n;
    /* the field and what plays in it (a person's words, an event's scene), and the day's card: there nothing
     * of the game's passes over its text. The menus' lists scroll under their frames, the tutorial lays a
     * page over the field, the pause is another picture: those stay in the composition */
    if (!enabled() || scale2d < 1 || n < scale2d * COARSER_BY ||
        (scene != KHDAYS_SIT_FIELD && scene != KHDAYS_SIT_DAY_COUNTER) || (states & (1u << KHDAYS_ST_EVENT)) != 0u) {
        return 0;
    }
    return n;
}

static float word(const uint32_t *words, int i)
{
    float v;
    memcpy(&v, words + i, sizeof(v));
    return v;
}

/* What a screen's master brightness (MASTER_BRIGHT: bits 0-4 the factor, 14-15 the mode) does to a piece
 * over a picture that already has it: toward black its colours go as the picture's do; toward white it lets
 * the whitened picture through by as much (its colours cannot be raised here), which ends at white as well. */
static void master_on(uint32_t master, float *alpha, float *shade)
{
    const uint32_t mode = (master >> 14) & 3u, factor = (master & 31u) > 16u ? 16u : master & 31u;
    if (mode == 1u) {
        *alpha *= 1.0f - (float)factor / 16.0f;
    } else if (mode == 2u) {
        *shade *= 1.0f - (float)factor / 16.0f;
    }
}

/* The master brightness the one-screen pass gives a texel of screen `lower` at picture column `x`
 * (gfx/onescreen.h ko_pixel's rules; a piece across the middle takes its own middle's half). */
static uint32_t master_at(const KhdaysOneScreenDraw *one, uint32_t upper_master, uint32_t lower_master, int lower,
                          float x)
{
    switch (one->rule) {
    case KOB_UPPER:
        return upper_master;
    case KOB_LOWER:
        return lower_master;
    case KOB_HALVES:
        return x < (float)one->columns / 2.0f ? upper_master : lower_master;
    case KOB_OWN:
        return lower ? lower_master : upper_master;
    default:
        return ((lower_master >> 14) & 3u) == 2u ? lower_master : upper_master;
    }
}

/* 0 when there is no room for it. */
static int add(uint32_t image, const float src[4], const float dst[4], float alpha, float shade)
{
    KhdaysTextPiece *piece;
    if (alpha <= 0.0f || src[2] <= 0.0f || src[3] <= 0.0f || dst[2] <= 0.0f || dst[3] <= 0.0f) {
        return 1;
    }
    if (piece_count >= PIECES_MAX) {
        return 0;
    }
    piece = &pieces[piece_count++];
    piece->image = image;
    memcpy(piece->src, src, sizeof(piece->src));
    memcpy(piece->dst, dst, sizeof(piece->dst));
    piece->alpha = alpha > 1.0f ? 1.0f : alpha;
    piece->shade = shade < 0.0f ? 0.0f : shade > 1.0f ? 1.0f : shade;
    return 1;
}

/* A layer's opacity where the game blends it over what is under it (the HUD's fade: gfx/frame.h KD_BLEND). */
static float layer_alpha(const uint32_t *desc, uint32_t layer)
{
    const uint32_t blend = desc[KD_BLEND], coef = desc[KD_COEF];
    if (((blend >> 16) & 3u) == 1u && (blend & (1u << layer)) != 0u && (blend & 0x2100u) != 0u) {
        return (float)((coef & 31u) > 16u ? 16u : coef & 31u) / 16.0f;
    }
    return 1.0f;
}

/* Whether a piece drawn flat over the shape's place shows what the pass would: not turned over, repeated or
 * with rounded corners, and without a fade along an edge. */
static int flat(const uint32_t *shape)
{
    const uint32_t flags = shape[KO_FLAGS];
    if ((flags & (KOF_MIRROR_X | KOF_MIRROR_Y | KOF_REPEAT_X | KOF_REPEAT_Y | KOF_ROUND)) != 0u ||
        word(shape, KO_SCALE_X) <= 0.0f || word(shape, KO_SCALE_Y) <= 0.0f) {
        return 0;
    }
    if ((flags & KOF_WEIGHT) != 0u) {
        for (int edge = 0; edge < 4; ++edge) {
            if (word(shape, KO_FADE + edge) != 0.0f) {
                return 0;
            }
        }
    }
    return 1;
}

/* What shape `shape` (a screen's rectangle) shows of the element `r` of that screen: 0 when nothing, else
 * its edges in the screen's DS dots. */
static int shown_of(const uint32_t *shape, const uint32_t *r, float *left, float *top, float *right, float *bottom)
{
    const float sx = word(shape, KO_SRC_X), sy = word(shape, KO_SRC_Y);
    const float sw = word(shape, KO_SRC_W), sh = word(shape, KO_SRC_H);
    const float ex = word(r, KE_X), ey = word(r, KE_Y), ew = word(r, KE_W), eh = word(r, KE_H);
    const float kx = word(shape, KO_SCALE_X), ky = word(shape, KO_SCALE_Y);
    /* what the shape's source holds of it, and of that what its destination holds */
    *left = ex > sx ? ex : sx;
    *top = ey > sy ? ey : sy;
    *right = ex + ew < sx + sw ? ex + ew : sx + sw;
    *bottom = ey + eh < sy + sh ? ey + eh : sy + sh;
    if (kx > 0.0f && ky > 0.0f) {
        const float wide = (word(shape, KO_DST_X2) - word(shape, KO_DST_X1)) / kx;
        const float high = (word(shape, KO_DST_Y2) - word(shape, KO_DST_Y1)) / ky;
        *right = *right < sx + wide ? *right : sx + wide;
        *bottom = *bottom < sy + high ? *bottom : sy + high;
    }
    return ew > 0.0f && eh > 0.0f && *right > *left && *bottom > *top;
}

/* Gives every element and shape back to the composition and the pass. */
static void give_back(KhdaysFramePackage *p, uint32_t *shapes, int shape_count)
{
    for (int e = 0; e < 2; ++e) {
        for (uint32_t i = 0; i < p->elements[e].count; ++i) {
            p->elements[e].records[(size_t)i * KE_WORDS + KE_SLOT] &= ~KES_OVERLAY;
        }
    }
    for (int s = 0; s < shape_count; ++s) {
        shapes[(size_t)s * KO_WORDS + KO_FLAGS] &= ~KOF_OVERLAY;
    }
    piece_count = 0;
}

void khdays_text_overlay_take(KhdaysFramePackage *p, const KhdaysOneScreenDraw *one, int allowed)
{
    /* (the shapes are the composition's own words, made for this picture: gpu_present.c present_now) */
    uint32_t *shapes = one != NULL ? (uint32_t *)one->shapes : NULL;
    const int shape_count = shapes != NULL ? one->shape_count : 0;
    const uint32_t upper_master = p->desc2d[p->upper][KD_MASTER] & 0xffffu;
    const uint32_t lower_master = p->desc2d[!p->upper][KD_MASTER] & 0xffffu;
    int room = 1;
    piece_count = 0;
    fresh = 1;
    if (!allowed || one == NULL) {
        give_back(p, shapes, shape_count);
        return;
    }
    /* first whose they are: an element a shape would not show flat stays the composition's, whole */
    for (int s = 0; s < shape_count; ++s) {
        uint32_t *shape = shapes + (size_t)s * KO_WORDS;
        const uint32_t flags = shape[KO_FLAGS];
        const int engine = (flags & KOF_LOWER) != 0u ? !p->upper : p->upper;
        if ((flags & KOF_3D) != 0u || flat(shape)) {
            continue;
        }
        if ((flags & KOF_IMAGE) != 0u) {
            shape[KO_FLAGS] &= ~KOF_OVERLAY;
            continue;
        }
        for (uint32_t i = 0; i < p->elements[engine].count; ++i) {
            uint32_t *r = p->elements[engine].records + (size_t)i * KE_WORDS;
            float left, top, right, bottom;
            if ((r[KE_SLOT] & (KES_OVERLAY | KES_PICTURE)) == KES_OVERLAY &&
                shown_of(shape, r, &left, &top, &right, &bottom)) {
                r[KE_SLOT] &= ~KES_OVERLAY;
            }
        }
    }
    for (int e = 0; e < 2; ++e) {
        for (uint32_t i = 0; i < p->elements[e].count; ++i) {
            uint32_t *r = p->elements[e].records + (size_t)i * KE_WORDS;
            if ((r[KE_SLOT] & KES_OVERLAY) != 0u && (r[KE_SLOT] & (KES_SPRITES | KES_DIRECT)) != KES_DIRECT) {
                r[KE_SLOT] &= ~KES_OVERLAY; /* (only colours in a layer's slot are drawn here) */
            }
        }
    }
    /* then where: the first shape that gives a texel is over the others, and so is the first element of a
     * slot: from the last of each, the topmost drawn last */
    for (int s = shape_count - 1; s >= 0 && room; --s) {
        const uint32_t *shape = shapes + (size_t)s * KO_WORDS;
        const uint32_t flags = shape[KO_FLAGS];
        const int lower = (flags & KOF_LOWER) != 0u;
        const int engine = lower ? !p->upper : p->upper;
        const float sx = word(shape, KO_SRC_X), sy = word(shape, KO_SRC_Y);
        const float dx = word(shape, KO_DST_X1), dy = word(shape, KO_DST_Y1);
        const float dx2 = word(shape, KO_DST_X2), dy2 = word(shape, KO_DST_Y2);
        const float kx = word(shape, KO_SCALE_X), ky = word(shape, KO_SCALE_Y);
        float alpha = (flags & KOF_WEIGHT) != 0u ? word(shape, KO_OPACITY) : 1.0f;
        float shade = (flags & KOF_DIM) != 0u ? word(shape, KO_OPACITY) : 1.0f; /* toward black */
        if ((flags & KOF_3D) != 0u || !flat(shape)) {
            continue;
        }
        master_on(master_at(one, upper_master, lower_master, lower, (dx + dx2) / 2.0f), &alpha, &shade);
        if ((flags & KOF_IMAGE) != 0u) {
            /* an image placed in the picture by itself (onescreen_images.c): the pass leaves it to us */
            if ((flags & KOF_OVERLAY) != 0u) {
                const float src[4] = {sx, sy, word(shape, KO_SRC_W), word(shape, KO_SRC_H)};
                const float dst[4] = {dx, dy, dx2 - dx, dy2 - dy};
                room = add(shape[KO_KEYS], src, dst,
                           alpha * ((flags & KOF_NATIVE_FADE) != 0u ? word(shape, KO_IMAGE_ALPHA) : 1.0f), shade);
            }
            continue;
        }
        for (int i = (int)p->elements[engine].count - 1; i >= 0 && room; --i) {
            const uint32_t *r = p->elements[engine].records + (size_t)i * KE_WORDS;
            float left, top, right, bottom;
            if ((r[KE_SLOT] & (KES_OVERLAY | KES_PICTURE)) == KES_OVERLAY &&
                shown_of(shape, r, &left, &top, &right, &bottom)) {
                const float ex = word(r, KE_X), ey = word(r, KE_Y);
                const float tx = (float)r[KE_IW] / word(r, KE_W), ty = (float)r[KE_IH] / word(r, KE_H);
                const float src[4] = {(left - ex) * tx, (top - ey) * ty, (right - left) * tx, (bottom - top) * ty};
                const float dst[4] = {dx + (left - sx) * kx, dy + (top - sy) * ky, (right - left) * kx,
                                      (bottom - top) * ky};
                room = add(r[KE_IMAGE], src, dst,
                           alpha * layer_alpha(p->desc2d[engine], r[KE_SLOT] & KES_NUMBER_MASK), shade);
            }
        }
    }
    if (!room) {
        give_back(p, shapes, shape_count); /* more pieces than are kept: none apart, rather than some lost */
    }
}

void khdays_text_overlay_clear(void)
{
    piece_count = 0;
}

const KhdaysTextPiece *khdays_text_overlay_pieces(int *count)
{
    *count = piece_count;
    return pieces;
}

void khdays_text_overlay_close(void)
{
    for (int i = 0; i < TEXTURES_MAX; ++i) {
        if (textures[i].texture != NULL) {
            SDL_DestroyTexture(textures[i].texture);
        }
    }
    SDL_zeroa(textures);
    textures_of = NULL;
}

/* Image `image` as a texture of the renderer; with `read`, its texels as they are now. */
static SDL_Texture *texture_of(SDL_Renderer *renderer, uint32_t image, int read)
{
    const KhdaysTextureReplacement *r = khdays_textures_replacement((int)image);
    Texture *t = NULL, *spare = NULL;
    if (r == NULL || r->rgba == NULL || r->width <= 0 || r->height <= 0) {
        return NULL;
    }
    for (int i = 0; i < TEXTURES_MAX; ++i) {
        if (textures[i].texture != NULL && textures[i].image == image) {
            t = &textures[i];
        } else if (spare == NULL || (spare->texture != NULL && textures[i].texture == NULL) ||
                   (spare->texture != NULL && textures[i].texture != NULL && textures[i].unused > spare->unused)) {
            spare = &textures[i]; /* a free one, else the one unused longest */
        }
    }
    if (t == NULL && read && spare != NULL && (spare->texture == NULL || spare->unused > 0)) {
        t = spare; /* (never one this draw's pieces use) */
    }
    if (t == NULL) {
        return NULL;
    }
    if (t->texture != NULL && (t->image != image || t->w != r->width || t->h != r->height)) {
        SDL_DestroyTexture(t->texture);
        t->texture = NULL;
    }
    if (t->texture == NULL) {
        if (!read) {
            return NULL;
        }
        t->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, r->width,
                                       r->height);
        if (t->texture == NULL) {
            return NULL;
        }
        SDL_SetTextureBlendMode(t->texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t->texture, SDL_SCALEMODE_LINEAR);
        t->image = image;
        t->w = r->width;
        t->h = r->height;
        t->read = 0;
    }
    if (read) {
        uint32_t version;
        khdays_textures_read_begin((int)image);
        version = khdays_textures_version((int)image);
        if ((!t->read || t->version != version) && SDL_UpdateTexture(t->texture, NULL, r->rgba, r->width * 4)) {
            t->version = version;
            t->read = 1;
        }
        khdays_textures_read_end((int)image);
    }
    t->unused = 0;
    return t->read ? t->texture : NULL;
}

void khdays_text_overlay_draw(SDL_Renderer *renderer, float x)
{
    if (renderer != textures_of) {
        SDL_zeroa(textures); /* another renderer: the one before took its textures with it */
        textures_of = renderer;
    }
    for (int i = 0; i < TEXTURES_MAX; ++i) {
        textures[i].unused += textures[i].texture != NULL;
    }
    if (fresh) {
        /* the images as they are at the first draw of these pieces; a picture kept keeps them */
        for (int i = 0; i < piece_count; ++i) {
            texture_of(renderer, pieces[i].image, 1);
        }
        fresh = 0;
    }
    for (int i = 0; i < piece_count; ++i) {
        const KhdaysTextPiece *piece = &pieces[i];
        SDL_Texture *texture = texture_of(renderer, piece->image, 0);
        if (texture != NULL) {
            const SDL_FRect from = {piece->src[0], piece->src[1], piece->src[2], piece->src[3]};
            const SDL_FRect at = {x + piece->dst[0], piece->dst[1], piece->dst[2], piece->dst[3]};
            SDL_SetTextureAlphaModFloat(texture, piece->alpha);
            SDL_SetTextureColorModFloat(texture, piece->shade, piece->shade, piece->shade);
            SDL_RenderTexture(renderer, texture, &from, &at);
        }
    }
    for (int i = 0; i < TEXTURES_MAX; ++i) {
        if (textures[i].texture != NULL && textures[i].unused > UNUSED_DRAWS) {
            SDL_DestroyTexture(textures[i].texture);
            textures[i].texture = NULL;
        }
    }
}
