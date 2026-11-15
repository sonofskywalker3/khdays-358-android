// The 2D composed once a DS dot (gfx/spread2d.h): a thread an output texel, after compose2d.hlsl's DS-sized pass. A dot
// alike at every place in it (PRE_SAME) gets its colour at each of its N x N output texels; one the one-screen picture's
// source leaves to the 3D (PRE_NOTHING) gets nothing there; one that depends on the 3D's alpha alone
// (PRE_NOTHING_BACKDROP, PRE_SAME_ALPHA3D) is worked out at each texel from the 3D there, or put on the list where it
// can't be; one whose 2D is the same at every place, on a screen showing the 3D with its layers (PRE_OVER3D), gets
// the 3D there put among the rest as the composition would (compose_core.h kd_over3d); any other is put on the list, and compose2d.hlsl then composes each of its output texels (gfx_render.c
// `sparse`). Small on purpose: the dots that need none of the
// composition are written without the whole of it in the shader.

#include "../gfx/frame.h"
#include "../gfx/spread2d.h"
#define KD_FN
#include "../gfx/compose_core.h"

[[vk::image_format("r32ui")]] RWTexture2D<uint> pre : register(u0, space1); // compose2d.hlsl's verdicts, then the list
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> out_screen : register(u1, space1);
// the 3D's targets and the HUD's 3D drawn apart, as compose2d.hlsl reads them (read only; with SPREAD_SOLID)
[[vk::image_format("r32ui")]] RWTexture2D<uint> color3d : register(u2, space1);
[[vk::image_format("r32ui")]] RWTexture2D<uint> attr3d : register(u3, space1);
[[vk::image_format("r32ui")]] RWTexture2D<uint> hud_color3d : register(u4, space1);
[[vk::image_format("r32ui")]] RWTexture2D<uint> hud_attr3d : register(u5, space1);
// the list's composition's groups, as an indirect dispatch takes them: x (SPREAD_GROUPS_X), y, z
RWByteAddressBuffer groups : register(u6, space1);

// compose2d.hlsl's own block, as its pass for this screen is given it (b0): N, the 3D's scale, offset and finishing
#include "compose2d_params.hlsli"

cbuffer Spread : register(b1, space2) {
    uint dots_w;    // the part's right and bottom edges, DS dots (within the screen)
    uint dots_h;
    uint flags;     // SPREAD_*
    uint from_x;    // the DS dot the dispatch starts at (a part of the screen)
    uint from_y;
    uint hofs3d;    // the description's KD_HOFS3D (SPREAD_SOLID)
    uint backdrop8; // the backdrop's colour, 8 bits a channel (compose.h kd_fine)
    uint pad0;
    // PRE_OVER3D's (SPREAD_SOLID): the description's KD_FLAGS, KD_BLEND, KD_COEF, KD_MASTER and KD_WIDE as the
    // composition of the screen takes them
    uint desc_flags;
    uint desc_blend;
    uint desc_coef;
    uint desc_master;
    uint desc_wide;
    uint pad1;
    uint pad2;
    uint pad3;
};

#define FINISH3D_SCALED
#include "finish3d.hlsli"
#include "raster3d_at.hlsli"

float channel8(uint c6)
{
    return (float)((c6 << 2) | (c6 >> 4)) / 255.0f; // as compose2d.hlsl's
}

// a composed colour as compose2d.hlsl writes it
float4 colour_of(uint c6)
{
    if ((flags & SPREAD_SPLIT) != 0u) {
        return float4((float)(c6 & 255u), (float)((c6 >> 8) & 255u), (float)((c6 >> 16) & 255u), (float)(c6 >> 24)) /
               255.0f;
    }
    if ((flags & SPREAD_FINE) != 0u) {
        return float4((float)(c6 & 255u) / 255.0f, (float)((c6 >> 8) & 255u) / 255.0f,
                      (float)((c6 >> 16) & 255u) / 255.0f, 1.0f);
    }
    return float4(channel8(c6 & 63u), channel8((c6 >> 8) & 63u), channel8((c6 >> 16) & 63u), 1.0f);
}

groupshared uint g_listed; // this group's dots for the list, then where they start in it
groupshared uint g_base;

[numthreads(8, 8, 1)]
void cs_main(uint3 dispatched : SV_DispatchThreadID, uint lane : SV_GroupIndex)
{
    if ((flags & SPREAD_RESET) != 0u) {
        // a thread each of the dots' flags' words (SPREAD_FLAG_WORDS of them), the first the list's count too
        const uint w = dispatched.y * 64u * 8u + dispatched.x;
        if (w < SPREAD_FLAG_WORDS) {
            pre[uint2(w % SPREAD_LIST_W, SPREAD_FLAG_ROW + w / SPREAD_LIST_W)] = 0u;
        }
        if (dispatched.x == 0u && dispatched.y == 0u) {
            pre[uint2(0u, SPREAD_LIST_ROW)] = 0u;
            groups.Store3(0u, uint3(SPREAD_GROUPS_X, 0u, 1u));
            if ((flags & SPREAD_STATS) != 0u) {
                for (uint b = 0u; b < 8u; ++b) {
                    pre[uint2(SPREAD_STATS_X + b, SPREAD_STATS_ROW)] = 0u;
                }
            }
        }
        return;
    }
    if (lane == 0u) {
        g_listed = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    // a thread an output texel (the dispatch covers a part of the screen from `origin`, in DS dots)
    const uint2 texel = dispatched.xy + uint2(from_x, from_y) * scale;
    const uint2 dot = texel / scale;
    const bool inside = dot.x < dots_w && dot.y < dots_h;
    const uint word = inside ? pre[uint2(dot.x * 2u, dot.y)] : PRE_SAME;
    const uint verdict = !inside ? PRE_SAME : (flags & SPREAD_ALL) != 0u ? PRE_EACH : word & PRE_VERDICT;
    // The 3D's alpha at this texel as kd_split takes it (kd_alpha3d, kd_moved3d: 31 beside the 256 columns, 0 where
    // moved), read as compose2d.hlsl reads it here (read3d_once); for the dots whose colour depends on it alone.
    // PRE_NOTHING_BACKDROP: kd_split for only the 3D over the backdrop, the backdrop a second target: 0 where the
    // 3D is opaque, absent or moved; translucent, its alpha blends the backdrop. PRE_SAME_ALPHA3D: the dot's colour
    // where the alpha is the one it was composed with, else composed here after all (the dot listed).
    bool solid = false;
    float4 behind = float4(0.0f, 0.0f, 0.0f, 0.0f);
    const bool by_alpha3d =
        inside && (verdict == PRE_NOTHING_BACKDROP || verdict == PRE_SAME_ALPHA3D) && (flags & SPREAD_SOLID) != 0u;
    // PRE_OVER3D: the dot's 2D, and the 3D at this texel put among it as the composition would (kd_over3d)
    const bool over = inside && verdict == PRE_OVER3D && (flags & SPREAD_SOLID) != 0u;
    KdOver3d o;
    const uint other = over ? pre[uint2(dot.x * 2u + 1u, dot.y)] : 0u;
    o.rank = (word >> PRE_OVER3D_RANK_SHIFT) & 3u;
    o.c_a = (word >> PRE_OVER3D_A_SHIFT) & 0x7fffu;
    o.c_b = other & 0x7fffu;
    o.layer_a = (other >> PRE_OVER3D_LAYER_A_SHIFT) & 7u;
    o.layer_b = (other >> PRE_OVER3D_LAYER_B_SHIFT) & 7u;
    o.spr_mode = (other >> PRE_OVER3D_MODE_SHIFT) & 3u;
    // the 3D's column as kd_pixel reads it (kd_pixel_wide: in the wider target; the one-screen picture's sources are
    // never wider), read once here for either: raster3d_at inlined twice made the shader half as large again
    uint column;
    bool in3d;
    if (desc_wide != 0u) {
        const int target = (int)dot.x + (hofs3d < 256u ? (int)hofs3d : (int)hofs3d - 512);
        column = (uint)target;
        in3d = target >= 0 && target < 256 + 2 * (int)desc_wide;
    } else {
        column = (dot.x + hofs3d) & 511u;
        in3d = column < 256u;
    }
    uint p3 = 0u;
    if ((by_alpha3d || (over && o.rank < 2u)) && in3d) {
        p3 = raster3d_at(column, (int)(dot.y + row0), texel.x % scale, texel.y % scale);
    }
    if (over) {
        // kd_pixel's flags without the layers left out (kd_pixel_wide takes them whole)
        const uint kd_flags = desc_wide != 0u ? desc_flags : desc_flags & ~layers_off;
        behind = colour_of(kd_over3d(kd_flags, desc_blend, desc_coef, desc_master,
                                     (word >> PRE_OVER3D_MASK_SHIFT) & 0x3fu, p3, in3d ? 1u : 0u, o));
    }
    if (by_alpha3d) {
        const uint a3 = !in3d ? 31u : (p3 & 0x20000000u) != 0u ? 0u : (p3 >> 24) & 31u;
        if (verdict == PRE_SAME_ALPHA3D) {
            solid = a3 == ((word >> PRE_A3_SHIFT) & 31u);
            if (solid) {
                behind = colour_of(pre[uint2(dot.x * 2u + 1u, dot.y)]);
            }
        } else {
            solid = true;
            if (a3 != 0u && a3 < 31u) {
                const uint eva = a3 / 2u;
                uint kb = 0u;
                for (uint sh = 0u; sh < 24u; sh += 8u) {
                    kb |= (((backdrop8 >> sh) & 255u) * (16u - eva) / 16u) << sh;
                }
                behind = colour_of(kb | ((1u + (eva * 254u + 8u) / 16u) << 24));
            }
        }
    }
    // the dot's first texel puts it on the list, once (a PRE_NOTHING_BACKDROP dot: the first texel that can't be
    // worked out here); the group's places on it taken together
    bool listed = false;
    // the listed dot's SPREAD_WHY_* (compose2d.hlsl's DS-sized pass; a PRE_OVER3D dot's word holds its 2D instead)
    const uint why = (word & PRE_VERDICT) == PRE_OVER3D ? SPREAD_WHY_3D : word >> PRE_WHY_SHIFT;
    const bool by_texel = verdict == PRE_NOTHING_BACKDROP || verdict == PRE_SAME_ALPHA3D;
    if (inside && by_texel && !solid) {
        const uint bit = dot.y * 1024u + dot.x, w = bit / 32u;
        uint was;
        InterlockedOr(pre[uint2(w % SPREAD_LIST_W, SPREAD_FLAG_ROW + w / SPREAD_LIST_W)], 1u << (bit & 31u), was);
        listed = (was & (1u << (bit & 31u))) == 0u;
    } else if (inside && verdict != PRE_SAME && verdict != PRE_NOTHING && !by_texel && !over) {
        listed = texel.x % scale == 0u && texel.y % scale == 0u;
    }
    uint mine = 0u;
    if (listed) {
        InterlockedAdd(g_listed, 1u, mine);
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0u && g_listed != 0u) {
        uint base;
        InterlockedAdd(pre[uint2(0u, SPREAD_LIST_ROW)], g_listed, base);
        g_base = base;
        // the groups the list needs so far: N x N threads a dot, 64 a group, SPREAD_GROUPS_X groups a row
        const uint threads = (base + g_listed) * scale * scale;
        const uint rows = ((threads + 63u) / 64u + SPREAD_GROUPS_X - 1u) / SPREAD_GROUPS_X;
        uint was;
        groups.InterlockedMax(4u, rows, was);
    }
    GroupMemoryBarrierWithGroupSync();
    if (!inside) {
        return;
    }
    if (listed) {
        const uint at = g_base + mine + 1u;
        pre[uint2(at % SPREAD_LIST_W, SPREAD_LIST_ROW + at / SPREAD_LIST_W)] = dot.x | dot.y << 16;
        if ((flags & SPREAD_STATS) != 0u) {
            uint was;
            for (uint b = 0u; b < SPREAD_WHYS; ++b) {
                if ((why & (1u << b)) != 0u) {
                    InterlockedAdd(pre[uint2(SPREAD_STATS_X + b, SPREAD_STATS_ROW)], 1u, was);
                }
            }
            InterlockedAdd(pre[uint2(SPREAD_STATS_X + 7u, SPREAD_STATS_ROW)], 1u, was);
        }
    } else if (verdict == PRE_SAME) {
        out_screen[texel] = colour_of(pre[uint2(dot.x * 2u + 1u, dot.y)]);
    } else if (verdict == PRE_NOTHING) {
        out_screen[texel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    } else if (solid || over) {
        out_screen[texel] = behind;
    }
}
