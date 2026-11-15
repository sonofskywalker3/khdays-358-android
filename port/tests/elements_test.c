/* The port's screen elements in the composition, on the CPU: fixed screens (a BG layer with a tile, and
 * the elements standing in for it) composed by port/gfx/compose.h as the game's frames are; and the 3D put among a
 * DS pixel's 2D (compose_core.h kd_over3d) against the composition.
 *   ctest --test-dir build/port/obj-x64 -R khdays-elements --output-on-failure */
#include "../gfx/describe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

#define W 256
#define H 192

static uint8_t io[0x2000];
static uint8_t oam[0x800]; /* every sprite's tile 0, which is empty */
static uint8_t palette[0x800];
static uint8_t bg_a[0x80000], obj_a[0x40000], bg_b[0x20000];
static KhdaysVramPages pages;
static uint32_t desc[KHDAYS_DESC_WORDS];
static KhdaysGfxLatch latch;
static uint32_t base_out[W * H], out[W * H];

/* the element images: texels by KE_IMAGE */
static uint32_t *images[8];

static const uint32_t *texels(uint32_t image)
{
    return image < 8 ? images[image] : NULL;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

static uint32_t fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

/* Engine A: the layers shown, BG1 a text layer at priority 1 (its tiles at
 * 0x4000, its map at 0xf800) with tile 1 -- every pixel colour 1, red -- at
 * cell (2, 3), the DS pixels 16-23 x 24-31; colour 2 green, 3 blue. */
static void scene(void)
{
    memset(io, 0, sizeof(io));
    memset(palette, 0, sizeof(palette));
    memset(bg_a, 0, sizeof(bg_a));
    memset(obj_a, 0, sizeof(obj_a));
    memset(bg_b, 0, sizeof(bg_b));
    put32(io, 0x00010000u | 0x200u | 0x1000u | 0x10u); /* display the layers; BG1 and OBJ on; OBJ 1D */
    put16(io + 0x0a, 1u | (1u << 2) | (31u << 8));     /* BG1CNT: priority 1, chars 0x4000, map 0xf800 */
    put16(palette + 2, 0x001f);                        /* red */
    put16(palette + 4, 0x03e0);                        /* green */
    put16(palette + 6, 0x7c00);                        /* blue */
    put16(palette + 0x200 + 2, 0x7fff);                /* sprite colour 1: white */
    memset(bg_a + 0x4000 + 32, 0x11, 32);              /* tile 1 */
    put16(bg_a + 0xf800 + (3 * 32 + 2) * 2, 1);        /* the map's cell (2, 3) */
    /* engine B: its BG0 the same way */
    put32(io + 0x1000, 0x00010000u | 0x100u);
    put16(io + 0x1008, (1u << 2) | (31u << 8));
    put16(palette + 0x400 + 2, 0x7c00); /* engine B's colour 1: blue */
    memset(bg_b + 0x4000 + 32, 0x11, 32);
    put16(bg_b + 0xf800 + (5 * 32 + 4) * 2, 1); /* cell (4, 5): 32-39 x 40-47 */
    for (int i = 0; i < 32; ++i) {
        pages.page[KHDAYS_AREA_ABG][i] = bg_a + i * KHDAYS_VRAM_PAGE;
    }
    for (int i = 0; i < 16; ++i) {
        pages.page[KHDAYS_AREA_AOBJ][i] = obj_a + i * KHDAYS_VRAM_PAGE;
    }
    for (int i = 0; i < 8; ++i) {
        pages.page[KHDAYS_AREA_BBG][i] = bg_b + i * KHDAYS_VRAM_PAGE;
    }
}

static void memory_of(KhdaysGfxMemory *m, const uint32_t *elements, const uint8_t *left_cells)
{
    memset(m, 0, sizeof(*m));
    m->vram = &pages;
    m->palette = palette;
    m->elements = elements;
    m->element_texels = texels;
    m->left_out_cells = left_cells;
}

static void compose(int engine, const uint32_t *elements, uint32_t count, const uint8_t *left_cells, uint32_t *to)
{
    KhdaysGfxMemory m;
    memset(&latch, 0, sizeof(latch));
    khdays_gfx_describe(io, palette, oam, engine, &latch, desc);
    desc[KD_ELEMENTS] = count;
    memory_of(&m, elements, left_cells);
    khdays_gfx_compose_cpu(desc, &m, to);
}

/* An indexed image of w x h texels, each `index` (0: transparent). */
static uint32_t *image(int n, int w, int h, uint32_t index)
{
    free(images[n]);
    images[n] = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    for (int i = 0; i < w * h; ++i) {
        images[n][i] = index != 0 ? KET_OPAQUE | index : 0;
    }
    return images[n];
}

static void element(uint32_t *r, uint32_t slot, float x, float y, float w, float h, int img, int iw, int ih)
{
    r[KE_SLOT] = slot;
    r[KE_X] = fbits(x);
    r[KE_Y] = fbits(y);
    r[KE_W] = fbits(w);
    r[KE_H] = fbits(h);
    r[KE_IMAGE] = (uint32_t)img;
    r[KE_IW] = (uint32_t)iw;
    r[KE_IH] = (uint32_t)ih;
}

/* the cell showing tile 1, (2, 3) of BG1's map, left out */
static uint8_t left_tile[KE_LEFT_CELL_WORDS * 4];

static int differences_outside(const uint32_t *a, const uint32_t *b, int x0, int y0, int x1, int y1, int x2, int y2,
                               int x3, int y3)
{
    int n = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int in_a = x >= x0 && x < x1 && y >= y0 && y < y1;
            const int in_b = x >= x2 && x < x3 && y >= y2 && y < y3;
            if (!in_a && !in_b && a[y * W + x] != b[y * W + x]) {
                ++n;
            }
        }
    }
    return n;
}

/* compose_core.h's kd_over3d (spread2d.hlsl's PRE_OVER3D texels): a DS pixel's 2D taken once (kd_over3d_of) and the
 * 3D put among it at each place gives what the composition does with that 3D -- kd_pixel itself where only the 3D
 * lies, and kd_compose's own choice and effects (kd_pick, kd_effects) with layers and a sprite above, between or
 * under it: every colour effect and its targets, a sprite's modes, the master brightness, 8-bit channels or the DS's,
 * the window mask, the 3D scrolled beside its columns, and 3D pixels opaque, translucent and absent. */
typedef uint32_t uint;
#define KD_FN static inline
#include "../gfx/compose_core.h"

static uint32_t layer3d[W * H];

static uint32_t next_random(uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return *seed >> 8;
}

static uint32_t random_3d(uint32_t *seed)
{
    const uint32_t pick = next_random(seed) % 4u;
    const uint32_t alpha = pick == 0 ? 0u : pick == 1 ? 31u : 1u + next_random(seed) % 30u;
    return (next_random(seed) & 0x3f3f3fu) | alpha << 24;
}

/* the description's effects, at random: flags (8-bit channels or not), BLDCNT with `effect`, BLDALPHA/BLDY, the
 * backdrop and the master brightness */
static void random_effects(uint32_t *d, uint32_t *seed, uint32_t fine, uint32_t effect)
{
    d[KD_FLAGS] = 1u | KDF_BG0_IS_3D | (1u << KDF_BG_ON_SHIFT) | (fine ? KDF_FINE : 0u);
    d[KD_BACKDROP] = next_random(seed) & 0x7fffu;
    d[KD_MASTER] = (next_random(seed) % 17u) | (next_random(seed) % 3u) << 14;
    d[KD_BLEND] = (next_random(seed) & 0x3f3fu) | effect << 16;
    d[KD_COEF] = (next_random(seed) % 17u) | (next_random(seed) % 17u) << 8 | (next_random(seed) % 17u) << 16;
}

static void over3d_matches_composition(void)
{
    static uint32_t d[KHDAYS_DESC_WORDS];
    uint32_t seed = 358u;
    long compared = 0, differing = 0;
    KhdaysGfxMemory m;
    KdSprite none;
    memset(&none, 0, sizeof(none));
    memory_of(&m, NULL, NULL);
    m.layer3d = layer3d;
    for (int i = 0; i < W * H; ++i) {
        layer3d[i] = random_3d(&seed);
    }
    /* only the 3D: kd_pixel, the whole composition on the CPU */
    for (int round = 0; round < 512; ++round) {
        const uint32_t windows = ((uint32_t)round >> 1) & 1u, hofs = ((uint32_t)round >> 4) & 1u ? 300u : 0u;
        const uint32_t prio = next_random(&seed) % 4u;
        uint32_t nothing[4] = {0, 0, 0, 0};
        memset(d, 0, sizeof(d));
        random_effects(d, &seed, (uint32_t)round & 1u, ((uint32_t)round >> 2) & 3u);
        d[KD_FLAGS] |= windows ? 1u << KDF_WIN_ON_SHIFT : 0u;
        d[KD_LAYERS + KDL_KIND] = KDK_3D | prio << 8;
        d[KD_WINOUT] = next_random(&seed) & 0x3fu; /* window 0 on no line: every pixel outside it */
        d[KD_HOFS3D] = hofs;
        for (int n = 0; n < 64; ++n) {
            const int x = (int)(next_random(&seed) % W), y = (int)(next_random(&seed) % H);
            const uint32_t column = ((uint32_t)x + hofs) & 511u;
            const uint32_t mask = windows ? d[KD_WINOUT] & 0x3fu : 0x3fu;
            const KdOver3d o = kd_over3d_of(nothing, none, mask, prio, d[KD_BACKDROP]);
            const uint32_t got = kd_over3d(d[KD_FLAGS], d[KD_BLEND], d[KD_COEF], d[KD_MASTER], mask,
                                           column < 256u ? layer3d[y * W + (int)column] : 0u, column < 256u, o);
            ++compared;
            differing += got != khdays_gfx_pixel_cpu(d, &m, x, y);
        }
    }
    /* layers and a sprite above, between and under the 3D: kd_compose's choice and effects at each 3D */
    for (int round = 0; round < 4096; ++round) {
        uint32_t color[4];
        KdSprite spr;
        const uint32_t mask = next_random(&seed) % 4u == 0 ? next_random(&seed) & 0x3fu : 0x3fu;
        const uint32_t prios = next_random(&seed) & 0xffu;
        memset(d, 0, sizeof(d));
        random_effects(d, &seed, (uint32_t)round & 1u, ((uint32_t)round >> 1) & 3u);
        color[0] = 0u;
        for (int b = 1; b < 4; ++b) {
            color[b] = next_random(&seed) % 3u == 0 ? 0u : KD_OPAQUE | (next_random(&seed) & 0x7fffu);
        }
        spr.color = next_random(&seed) % 2u == 0 ? 0u : KD_OPAQUE | (next_random(&seed) & 0x7fffu);
        spr.prio = next_random(&seed) % 4u;
        spr.mode = next_random(&seed) % 4u;
        spr.window = 0u;
        {
            const KdOver3d o = kd_over3d_of(color, spr, mask, prios, d[KD_BACKDROP]);
            for (int n = 0; n < 8; ++n) {
                const uint32_t p3 = random_3d(&seed), in3d = next_random(&seed) % 8u != 0u;
                const uint32_t alpha = in3d ? (p3 >> 24) & 31u : 31u;
                uint32_t with3d[4];
                KdPick t;
                uint32_t want;
                memcpy(with3d, color, sizeof(with3d));
                with3d[0] = in3d && alpha != 0u
                                ? KD_OPAQUE | ((p3 >> 1) & 31u) | (((p3 >> 9) & 31u) << 5) | (((p3 >> 17) & 31u) << 10)
                                : 0u;
                t = kd_pick(with3d, spr, mask, prios, d[KD_BACKDROP]);
                want = kd_effects(d[KD_FLAGS], d[KD_BLEND], d[KD_COEF], mask, t.layer0, t.c0, t.layer1, t.c1, spr.mode,
                                  alpha, in3d ? p3 & 0xffffffu : 0u);
                want = (d[KD_FLAGS] & KDF_FINE) != 0u ? kd_bright8(want, d[KD_MASTER]) : kd_bright(want, d[KD_MASTER]);
                ++compared;
                differing += kd_over3d(d[KD_FLAGS], d[KD_BLEND], d[KD_COEF], d[KD_MASTER], mask, p3, in3d, o) != want;
            }
        }
    }
    CHECK(compared == 512 * 64 + 4096 * 8);
    CHECK(differing == 0);
}

int main(void)
{
    uint32_t r[KE_WORDS * 2];
    memset(left_tile, 0, sizeof(left_tile));
    left_tile[(1 * KE_CELLS + 3 * 64 + 2) >> 3] |= (uint8_t)(1u << ((1 * KE_CELLS + 3 * 64 + 2) & 7));

    /* the DS's layout: the element over what it stands for gives the frame without it */
    scene();
    compose(0, NULL, 0, NULL, base_out);
    CHECK((base_out[24 * W + 16] & 0xff0000u) != 0 && (base_out[24 * W + 16] & 0xffffu) == 0); /* red */
    image(1, 8, 8, 1);
    element(r, 1, 16, 24, 8, 8, 1, 8, 8);
    compose(0, r, 1, left_tile, out);
    CHECK(memcmp(out, base_out, sizeof(out)) == 0);

    /* its image at 4x: the same at 1x */
    image(2, 32, 32, 1);
    element(r, 1, 16, 24, 8, 8, 2, 32, 32);
    compose(0, r, 1, left_tile, out);
    CHECK(memcmp(out, base_out, sizeof(out)) == 0);

    /* moved: only its old place and its new one change */
    element(r, 1, 100, 50, 8, 8, 1, 8, 8);
    compose(0, r, 1, left_tile, out);
    CHECK(differences_outside(out, base_out, 16, 24, 24, 32, 100, 50, 108, 58) == 0);
    CHECK(out[24 * W + 16] == base_out[0]);            /* its old place: the backdrop */
    CHECK(out[50 * W + 100] == base_out[24 * W + 16]); /* its new one: red */

    /* scaled twice over: 16x16 */
    element(r, 1, 100, 50, 16, 16, 1, 8, 8);
    compose(0, r, 1, left_tile, out);
    CHECK(out[65 * W + 115] == base_out[24 * W + 16]);
    CHECK(out[66 * W + 116] == base_out[0]);

    /* a transparent texel shows the layer under it */
    image(3, 8, 8, 0);
    element(r, 1, 16, 24, 8, 8, 3, 8, 8);
    compose(0, r, 1, NULL, out);
    CHECK(memcmp(out, base_out, sizeof(out)) == 0);

    /* the topmost first: green over red */
    image(4, 8, 8, 2);
    element(r, 1, 16, 24, 8, 8, 4, 8, 8);
    element(r + KE_WORDS, 1, 16, 24, 8, 8, 1, 8, 8);
    compose(0, r, 2, left_tile, out);
    CHECK((out[24 * W + 16] & 0x00ff00u) != 0 && (out[24 * W + 16] & 0xff0000u) == 0);

    /* brightened (BLDCNT BG1 first target, EVY 8), faded (master brightness),
     * a window over half the screen: still the frame without the element */
    scene();
    put16(io + 0x50, 0x0002u | (2u << 6));
    put16(io + 0x54, 8);
    put16(io + 0x6c, (1u << 14) | 6u);
    put32(io, 0x00010000u | 0x200u | 0x2000u);
    put16(io + 0x40, (0u << 8) | 20u);  /* WIN0H: 0-19 */
    put16(io + 0x44, (0u << 8) | 192u); /* WIN0V */
    put16(io + 0x48, 0x0002u);          /* WININ: BG1 inside window 0 */
    put16(io + 0x4a, 0x0000u);          /* WINOUT: nothing */
    compose(0, NULL, 0, NULL, base_out);
    image(1, 8, 8, 1);
    element(r, 1, 16, 24, 8, 8, 1, 8, 8);
    compose(0, r, 1, left_tile, out);
    CHECK(memcmp(out, base_out, sizeof(out)) == 0);

    /* in the sprites' slot, priority 0: drawn with the sprites' palette */
    scene();
    compose(0, NULL, 0, NULL, base_out);
    element(r, KES_SPRITES | 0u, 40, 40, 8, 8, 1, 8, 8);
    compose(0, r, 1, NULL, out);
    CHECK(out[40 * W + 40] != base_out[40 * W + 40] && (out[40 * W + 40] & 0xff) != 0 &&
          (out[40 * W + 40] & 0xff00) != 0);
    CHECK(differences_outside(out, base_out, 40, 40, 48, 48, 0, 0, 0, 0) == 0);

    /* the lower screen (engine B) */
    scene();
    compose(1, NULL, 0, NULL, base_out);
    CHECK((base_out[40 * W + 32] & 0xff) != 0);
    {
        uint8_t left_b[KE_LEFT_CELL_WORDS * 4];
        memset(left_b, 0, sizeof(left_b));
        left_b[(5 * 64 + 4) >> 3] |= (uint8_t)(1u << ((5 * 64 + 4) & 7)); /* BG0's cell (4, 5) */
        element(r, 0, 32, 40, 8, 8, 1, 8, 8);
        compose(1, r, 1, left_b, out);
        CHECK(memcmp(out, base_out, sizeof(out)) == 0);
    }

    /* A native mission-panel image remains visible on engine B's BG2 in dual-screen mode. */
    scene();
    put32(io + 0x1000, 0x00010000u | 0x700u);
    put16(io + 0x100a, 1u | (1u << 2) | (30u << 8));
    put16(io + 0x100c, 2u | (1u << 2) | (29u << 8));
    image(1, 8, 8, 0);
    for (int i = 0; i < 64; ++i)
        images[1][i] = 0xff00ff00u;
    element(r, 2 | KES_DIRECT | KES_PICTURE | KES_FINE, 16, 152, 8, 8, 1, 8, 8);
    compose(1, r, 1, NULL, out);
    CHECK(out[152 * W + 16] != out[0]);

    _Static_assert((KES_FINE & KES_PICTURE) == 0u, "image sampling and placement flags must be independent");
    /* Independent images are drawn separately from the one-screen source crop. */
    scene();
    image(1, 8, 8, 1);
    element(r, 1 | KES_PICTURE | KES_HUD, 40, 40, 8, 8, 1, 8, 8);
    compose(0, r, 1, NULL, out);
    CHECK(out[40 * W + 40] != out[0]);
    desc[KD_FLAGS] |= KDF_SPLIT | KDF_FINE;
    {
        KhdaysGfxMemory m;
        memory_of(&m, r, NULL);
        CHECK(khdays_gfx_pixel_cpu(desc, &m, 40, 40) == 0);
        r[KE_SLOT] &= ~KES_PICTURE;
        r[KE_SLOT] |= KES_FINE;
        CHECK(khdays_gfx_pixel_cpu(desc, &m, 40, 40) != 0);
    }
    over3d_matches_composition();
    for (int i = 0; i < 8; ++i)
        free(images[i]);
    if (failures) {
        fprintf(stderr, "%d failed\n", failures);
        return 1;
    }
    printf("elements: all passed\n");
    return 0;
}
