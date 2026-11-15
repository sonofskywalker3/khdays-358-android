/* The colour arithmetic of the 2D's composition (compose.h, which includes it): the DS's blends, brightness and
 * master brightness on a pixel's colours, and what kd_compose makes of the two topmost things at a pixel. Shared
 * C/HLSL, as compose.h; it reads nothing but its arguments, so a shader that only finishes pixels
 * (shaders/spread2d.hlsl) takes it without the rest. kd_compose is kd_pick, then kd_effects. The includer defines
 * KD_FN and the type `uint`, and the description's flags (frame.h). */
#ifndef KHDAYS_GFX_COMPOSE_CORE_H
#define KHDAYS_GFX_COMPOSE_CORE_H

#define KD_OPAQUE 0x8000u

/* --- Tiles drawn instead (HD 2D from mods; not the DS) ----------------------------------
 * A layer's or a sprite's colour is KD_OPAQUE | BGR555, or, from a replaced
 * tile in its own colours, KD_HD | RGB888 (red in bits 0-7). */
#define KD_HD 0x80000000u

KD_FN uint kd_shown(uint c)
{
    return (c & (KD_OPAQUE | KD_HD)) != 0u ? 1u : 0u;
}
KD_FN uint kd_keep(uint c)
{
    return (c & KD_HD) != 0u ? c : (c & 0x7fffu);
}

KD_FN uint kd_channel(uint c, uint shift)
{
    return (c >> shift) & 31u;
}

KD_FN uint kd_blend(uint a, uint b, uint eva, uint evb)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 15u; shift += 5u) {
        const uint v = (kd_channel(a, shift) * eva + kd_channel(b, shift) * evb) >> 4;
        acc |= (v > 31u ? 31u : v) << shift;
    }
    return acc;
}

KD_FN uint kd_brighten(uint a, uint evy)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 15u; shift += 5u) {
        const uint v = kd_channel(a, shift);
        acc |= (v + (((31u - v) * evy) >> 4)) << shift;
    }
    return acc;
}

KD_FN uint kd_darken(uint a, uint evy)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 15u; shift += 5u) {
        const uint v = kd_channel(a, shift);
        acc |= (v - ((v * evy) >> 4)) << shift;
    }
    return acc;
}

/* The master brightness on the 6-bit intensities: R in bits 0-5, G 8-13, B 16-21. */
KD_FN uint kd_bright(uint color, uint master)
{
    const uint factor = master & 31u;
    const uint mode = (master >> 14) & 3u;
    uint acc = 0u;
    uint n;
    for (n = 0u; n < 3u; ++n) {
        const uint c5 = kd_channel(color, 5u * n);
        uint c = c5 != 0u ? c5 * 2u + 1u : 0u;
        if (mode == 1u) {
            c += (63u - c) * factor / 16u;
        } else if (mode == 2u) {
            c -= c * factor / 16u;
        }
        acc |= c << (8u * n);
    }
    return acc;
}

/* --- In 8 bits per channel (KDF_FINE: when HD textures are drawn; not the DS) -------
 * RGB888 with red in bits 0-7, green 8-15, blue 16-23: the same effects on
 * wider channels. */

KD_FN uint kd_fine(uint bgr555)
{
    uint acc = 0u;
    uint n;
    for (n = 0u; n < 3u; ++n) {
        const uint c = kd_channel(bgr555, 5u * n);
        acc |= ((c << 3) | (c >> 2)) << (8u * n);
    }
    return acc;
}

KD_FN uint kd_blend8(uint a, uint b, uint eva, uint evb)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 24u; shift += 8u) {
        const uint v = (((a >> shift) & 255u) * eva + ((b >> shift) & 255u) * evb) >> 4;
        acc |= (v > 255u ? 255u : v) << shift;
    }
    return acc;
}

KD_FN uint kd_brighten8(uint a, uint evy)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 24u; shift += 8u) {
        const uint v = (a >> shift) & 255u;
        acc |= (v + (((255u - v) * evy) >> 4)) << shift;
    }
    return acc;
}

KD_FN uint kd_darken8(uint a, uint evy)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 24u; shift += 8u) {
        const uint v = (a >> shift) & 255u;
        acc |= (v - ((v * evy) >> 4)) << shift;
    }
    return acc;
}

KD_FN uint kd_bright8(uint color, uint master)
{
    const uint factor = master & 31u;
    const uint mode = (master >> 14) & 3u;
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 24u; shift += 8u) {
        uint c = (color >> shift) & 255u;
        if (mode == 1u) {
            c += (255u - c) * factor / 16u;
        } else if (mode == 2u) {
            c -= c * factor / 16u;
        }
        acc |= c << shift;
    }
    return acc;
}

#define KD_LAYER_SPRITE 4u
#define KD_LAYER_BACKDROP 5u

/* The topmost sprite at a pixel (compose.h kd_sprites). */
typedef struct KdSprite {
    uint color; /* KD_OPAQUE | BGR555 of the topmost, or 0 */
    uint prio;
    uint mode;
    uint window; /* 1: a sprite-window sprite covers the pixel */
} KdSprite;

/* What kd_compose makes of the two topmost things that show at a pixel (layer0's colour c0 over layer1's c1, as
 * kd_keep leaves them; KD_LAYER_SPRITE, KD_LAYER_BACKDROP or a layer's number): the colour effects the window
 * mask lets apply, the 3D's own alpha over a second target. BGR555; with KDF_FINE RGB888, the 3D layer's own
 * 8-bit colour `fine3d`. The description's flags, BLDCNT (`blendw`) and BLDALPHA/BLDY (`coef`) as KD_FLAGS,
 * KD_BLEND and KD_COEF hold them. */
KD_FN uint kd_effects(uint flags, uint blendw, uint coef, uint mask, uint layer0, uint c0, uint layer1, uint c1,
                      uint spr_mode, uint alpha3d, uint fine3d)
{
    const uint eva = coef & 31u, evb = (coef >> 8) & 31u, evy = (coef >> 16) & 31u;
    const uint effect = (blendw >> 16) & 3u;
    uint result;
    if ((flags & KDF_FINE) != 0u) {
        const uint is3d = (flags & KDF_BG0_IS_3D) != 0u ? 1u : 0u;
        const uint second = (blendw >> 8) & (1u << layer1);
        const uint f0 = is3d != 0u && layer0 == 0u ? fine3d : (c0 & KD_HD) != 0u ? c0 & 0xffffffu : kd_fine(c0);
        const uint f1 = is3d != 0u && layer1 == 0u ? fine3d : (c1 & KD_HD) != 0u ? c1 & 0xffffffu : kd_fine(c1);
        if (is3d != 0u && layer0 == 0u && second != 0u && (alpha3d < 31u || (effect == 1u && (blendw & 1u)))) {
            return alpha3d < 31u ? kd_blend8(f0, f1, alpha3d / 2u, 16u - alpha3d / 2u) : f0;
        }
        if (mask & 0x20u) {
            if (layer0 == KD_LAYER_SPRITE && (spr_mode == 1u || spr_mode == 3u) && second != 0u) {
                return kd_blend8(f0, f1, eva, evb);
            }
            if (blendw & (1u << layer0)) {
                if (effect == 1u && second != 0u)
                    return kd_blend8(f0, f1, eva, evb);
                if (effect == 2u)
                    return kd_brighten8(f0, evy);
                if (effect == 3u)
                    return kd_darken8(f0, evy);
            }
        }
        return f0;
    }
    /* a replaced tile's own colour, cut down to the DS's 5 bits */
    if ((c0 & KD_HD) != 0u) {
        c0 = ((c0 >> 3) & 31u) | (((c0 >> 11) & 31u) << 5) | (((c0 >> 19) & 31u) << 10);
    }
    if ((c1 & KD_HD) != 0u) {
        c1 = ((c1 >> 3) & 31u) | (((c1 >> 11) & 31u) << 5) | (((c1 >> 19) & 31u) << 10);
    }
    {
        const uint is3d = (flags & KDF_BG0_IS_3D) != 0u ? 1u : 0u;
        const uint second = (blendw >> 8) & (1u << layer1);
        if (is3d != 0u && layer0 == 0u && second != 0u && (alpha3d < 31u || (effect == 1u && (blendw & 1u)))) {
            /* the 3D over a second target blends by its own alpha, never
             * BLDALPHA's (GBATEK: probably EVA = A/2, EVB = 16 - A/2): an
             * opaque 3D pixel stays itself */
            result = alpha3d < 31u ? kd_blend(c0, c1, alpha3d / 2u, 16u - alpha3d / 2u) : c0;
        } else {
            result = c0;
            if (mask & 0x20u) {
                if (layer0 == KD_LAYER_SPRITE && (spr_mode == 1u || spr_mode == 3u) && second != 0u) {
                    result = kd_blend(c0, c1, eva, evb);
                } else if (blendw & (1u << layer0)) {
                    if (effect == 1u && second != 0u) {
                        result = kd_blend(c0, c1, eva, evb);
                    } else if (effect == 2u) {
                        result = kd_brighten(c0, evy);
                    } else if (effect == 3u) {
                        result = kd_darken(c0, evy);
                    }
                }
            }
        }
    }
    return result;
}

/* The two topmost things that show at a pixel, as kd_compose picks them, and their colours as kd_keep leaves them:
 * a sprite before the layers of its priority, the layers by number within a priority, the backdrop below all. The
 * layers' priorities 2 bits each (layer b's at bit 2b). */
typedef struct KdPick {
    uint layer0;
    uint c0;
    uint layer1;
    uint c1;
} KdPick;

KD_FN KdPick kd_pick(uint color[4], KdSprite spr, uint mask, uint prios, uint backdrop)
{
    KdPick t;
    uint found = 0u, p;
    int b;
    t.layer0 = KD_LAYER_BACKDROP;
    t.layer1 = KD_LAYER_BACKDROP;
    t.c0 = backdrop;
    t.c1 = backdrop;
    for (p = 0u; p < 4u && found < 2u; ++p) {
        if (kd_shown(spr.color) != 0u && spr.prio == p && (mask & 0x10u)) {
            if (found == 0u) {
                t.layer0 = KD_LAYER_SPRITE;
                t.c0 = kd_keep(spr.color);
            } else {
                t.layer1 = KD_LAYER_SPRITE;
                t.c1 = kd_keep(spr.color);
            }
            ++found;
        }
        for (b = 0; b < 4 && found < 2u; ++b) {
            if (((prios >> (2u * (uint)b)) & 3u) == p && kd_shown(color[b]) != 0u && (mask & (1u << (uint)b))) {
                if (found == 0u) {
                    t.layer0 = (uint)b;
                    t.c0 = kd_keep(color[b]);
                } else {
                    t.layer1 = (uint)b;
                    t.c1 = kd_keep(color[b]);
                }
                ++found;
            }
        }
    }
    return t;
}

/* A pixel whose 2D is the same at every place in it (nothing finer than the DS pixel, no element or replaced
 * tile's texel) on a screen whose BG0 is the 3D (KDF_BG0_IS_3D, showing its layers, not KDF_SPLIT's): only the 3D
 * varies there. What kd_compose needs of the rest: the two topmost things without the 3D (a, then b; their colours
 * in 15 bits, as no replaced tile is among them), the sprite's mode, and where the 3D comes when it shows (`rank`:
 * 0 on top, 1 under a only, 2 under both -- then it never decides the colour). shaders/spread2d.hlsl gives each
 * place of such a dot its colour by kd_over3d, without the composition. */
typedef struct KdOver3d {
    uint rank;
    uint layer_a;
    uint c_a;
    uint layer_b;
    uint c_b;
    uint spr_mode;
} KdOver3d;

KD_FN KdOver3d kd_over3d_of(uint color[4], KdSprite spr, uint mask, uint prios, uint backdrop)
{
    KdOver3d o;
    KdPick no3d, with3d;
    uint two[4];
    two[0] = 0u;
    two[1] = color[1];
    two[2] = color[2];
    two[3] = color[3];
    no3d = kd_pick(two, spr, mask, prios, backdrop & 0x7fffu);
    two[0] = KD_OPAQUE;
    with3d = kd_pick(two, spr, mask, prios, backdrop & 0x7fffu);
    o.rank = with3d.layer0 == 0u ? 0u : with3d.layer1 == 0u ? 1u : 2u;
    o.layer_a = no3d.layer0;
    o.c_a = no3d.c0 & 0x7fffu;
    o.layer_b = no3d.layer1;
    o.c_b = no3d.c1 & 0x7fffu;
    o.spr_mode = spr.mode & 3u;
    return o;
}

/* Its colour at a place whose 3D is p3 (KD_3D's value; `in3d` 0 beside the 3D's columns, where there is none), as
 * kd_pixel and kd_pixel_wide give it: the 3D among the rest by kd_over3d_of's rank, the effects by the window mask
 * the composition took there, the master brightness last. The description's KD_FLAGS, KD_BLEND, KD_COEF and
 * KD_MASTER. */
KD_FN uint kd_over3d(uint flags, uint blendw, uint coef, uint master, uint mask, uint p3, uint in3d, KdOver3d o)
{
    uint layer0 = o.layer_a, c0 = o.c_a, layer1 = o.layer_b, c1 = o.c_b, alpha3d = 31u, fine3d = 0u, result;
    if ((flags & (1u << KDF_BG_ON_SHIFT)) != 0u && in3d != 0u && ((p3 >> 24) & 31u) != 0u && (mask & 1u) != 0u &&
        o.rank < 2u) {
        const uint c3 = ((p3 >> 1) & 31u) | (((p3 >> 9) & 31u) << 5) | (((p3 >> 17) & 31u) << 10);
        alpha3d = (p3 >> 24) & 31u;
        fine3d = p3 & 0xffffffu;
        if (o.rank == 0u) {
            layer1 = layer0;
            c1 = c0;
            layer0 = 0u;
            c0 = c3;
        } else {
            layer1 = 0u;
            c1 = c3;
        }
    }
    result = kd_effects(flags, blendw, coef, mask, layer0, c0, layer1, c1, o.spr_mode, alpha3d, fine3d);
    return (flags & KDF_FINE) != 0u ? kd_bright8(result, master) : kd_bright(result, master);
}

#endif
