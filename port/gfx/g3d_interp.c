/* A picture between two of the game's 3D lists (see g3d_interp.h). */
#include "g3d_interp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t key_hash(const KhdaysG3dPreInfo *info, int32_t flags)
{
    uint32_t h = (uint32_t)info->draw * 2654435761u;
    h ^= info->occurrence * 0x85ebca6bu;
    h ^= info->ordinal * 0xc2b2ae35u;
    h ^= (uint32_t)(flags & KHDAYS_G3D_MOD) * 0x27d4eb2fu;
    return h ^ (h >> 15);
}

static int same_key(const KhdaysG3dPreInfo *a, int32_t fa, const KhdaysG3dPreInfo *b, int32_t fb)
{
    return a->draw == b->draw && a->occurrence == b->occurrence && a->ordinal == b->ordinal &&
           (fa & KHDAYS_G3D_MOD) == (fb & KHDAYS_G3D_MOD);
}

/* The older list's polygons in `work->slots`, found by key. */
static int index_older(KhdaysG3dInterp *work, const KhdaysG3dList *older)
{
    int32_t size = 64;
    while (size < older->npre * 2) {
        size *= 2;
    }
    if (size > work->slot_capacity) {
        int32_t *grown = (int32_t *)realloc(work->slots, sizeof(int32_t) * (size_t)size);
        if (grown == NULL) {
            return 0;
        }
        work->slots = grown;
        work->slot_capacity = size;
    }
    memset(work->slots, 0, sizeof(int32_t) * (size_t)size);
    for (int32_t i = 0; i < older->npre; ++i) {
        uint32_t at = key_hash(&older->pre_info[i], older->pre[i].flags) & (uint32_t)(size - 1);
        while (work->slots[at] != 0) {
            at = (at + 1) & (uint32_t)(size - 1);
        }
        work->slots[at] = i + 1;
    }
    return size;
}

/* The older list's polygon with `p`'s key, drawn as `p` is -- the same
 * texture, palette and attributes but alpha (a fade), the same texture drawn
 * instead --, or -1: a key that names another polygon (geometry that came or
 * went before it) is not taken for this one. */
static int32_t find_older(const KhdaysG3dInterp *work, int32_t size, const KhdaysG3dList *older,
                          const KhdaysG3dPreInfo *info, const KhdaysG3dPolygon *p)
{
    uint32_t at = key_hash(info, p->flags) & (uint32_t)(size - 1);
    while (work->slots[at] != 0) {
        const int32_t i = work->slots[at] - 1;
        const KhdaysG3dPolygon *q = &older->pre[i];
        if (same_key(&older->pre_info[i], q->flags, info, p->flags)) {
            const uint32_t alpha = 0x1fu << 16;
            if (q->tex != p->tex || q->pltt != p->pltt || (q->attr & ~alpha) != (p->attr & ~alpha) ||
                q->replacement != p->replacement) {
                return -1;
            }
            return i;
        }
        at = (at + 1) & (uint32_t)(size - 1);
    }
    return -1;
}

/* A vertex's place on the view, x/w and y/w (-1..1 the view); 0 when it is
 * behind the eye. A mod's words are floats, the game's fixed point. */
static int view_place(const KhdaysG3dPreVertex *v, int32_t flags, double *x, double *y)
{
    double c[4];
    for (int i = 0; i < 4; ++i) {
        if (flags & KHDAYS_G3D_MOD) {
            float f;
            memcpy(&f, &v->word[i], sizeof(f));
            c[i] = f;
        } else {
            c[i] = (double)v->word[i];
        }
    }
    if (c[3] <= 0.0)
        return 0;
    *x = c[0] / c[3];
    *y = c[1] / c[3];
    return 1;
}

/* Whether a polygon's first vertex moved more than a quarter of the view between two lists. */
static int moved_far(const KhdaysG3dPreVertex *a, const KhdaysG3dPreVertex *b, int32_t flags)
{
    double ax, ay, bx, by;
    if (!view_place(a, flags, &ax, &ay) || !view_place(b, flags, &bx, &by))
        return 0;
    return (ax - bx) * (ax - bx) + (ay - by) * (ay - by) > 0.5 * 0.5;
}

static int32_t between_fixed(int32_t a, int32_t b, double t)
{
    return (int32_t)llround((double)a + ((double)b - (double)a) * t); /* t = 1: b exactly */
}

int khdays_g3d_interpolate(KhdaysG3dInterp *work, const KhdaysG3dList *older, const KhdaysG3dList *newer, float alpha,
                           KhdaysG3dList *out)
{
    int32_t size;
    const double t = alpha < 0.0f ? 0.0 : alpha > 1.0f ? 1.0 : (double)alpha;
    work->matched = 0;
    work->jumped = 0;
    work->jumped_mod = 0;
    memset(work->jumped_in, 0, sizeof(work->jumped_in));
    work->polygons = newer->npre;
    if (older == NULL || t >= 1.0 || older->npre == 0) {
        return khdays_g3d_finish(newer, NULL, out);
    }
    if (newer->npre_verts > work->vert_capacity) {
        KhdaysG3dPreVertex *grown =
            (KhdaysG3dPreVertex *)realloc(work->verts, sizeof(KhdaysG3dPreVertex) * (size_t)newer->npre_verts);
        if (grown == NULL) {
            khdays_g3d_finish(newer, NULL, out);
            return 0;
        }
        work->verts = grown;
        work->vert_capacity = newer->npre_verts;
    }
    size = index_older(work, older);
    if (size == 0) {
        khdays_g3d_finish(newer, NULL, out);
        return 0;
    }
    memcpy(work->verts, newer->pre_verts, sizeof(KhdaysG3dPreVertex) * (size_t)newer->npre_verts);
    for (int32_t i = 0; i < newer->npre; ++i) {
        const KhdaysG3dPolygon *p = &newer->pre[i];
        const int32_t j = find_older(work, size, older, &newer->pre_info[i], p);
        const KhdaysG3dPolygon *q;
        if (j < 0 || (q = &older->pre[j])->count != p->count) {
            continue;
        }
        ++work->matched;
        if (moved_far(&older->pre_verts[q->first], &newer->pre_verts[p->first], p->flags)) {
            ++work->jumped;
            work->jumped_mod += (p->flags & KHDAYS_G3D_MOD) != 0;
            for (int d = 0; d < 4; ++d) {
                const KhdaysG3dPreInfo *in = &newer->pre_info[i];
                if (work->jumped_in[d].count == 0) {
                    work->jumped_in[d].draw = in->draw;
                    work->jumped_in[d].occurrence = in->occurrence;
                }
                if (work->jumped_in[d].draw == in->draw && work->jumped_in[d].occurrence == in->occurrence) {
                    ++work->jumped_in[d].count;
                    break;
                }
            }
        }
        for (int32_t k = 0; k < p->count; ++k) {
            const KhdaysG3dPreVertex *a = &older->pre_verts[q->first + k];
            KhdaysG3dPreVertex *b = &work->verts[p->first + k];
            if (p->flags & KHDAYS_G3D_MOD) {
                float fa[10], fb[10];
                memcpy(fa, a->word, sizeof(fa));
                memcpy(fb, b->word, sizeof(fb));
                for (int w = 0; w < 10; ++w) {
                    fb[w] = (float)((double)fa[w] + ((double)fb[w] - (double)fa[w]) * t);
                }
                memcpy(b->word, fb, sizeof(fb));
            } else {
                for (int w = 0; w < 10; ++w) {
                    b->word[w] = between_fixed(a->word[w], b->word[w], t);
                }
            }
        }
    }
    return khdays_g3d_finish(newer, work->verts, out);
}

void khdays_g3d_interp_free(KhdaysG3dInterp *work)
{
    free(work->verts);
    free(work->slots);
    memset(work, 0, sizeof(*work));
}
