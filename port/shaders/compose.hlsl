// Engine A's screen at N times the resolution: the 3D raster3d.hlsl drew, finished as gpu3d.c does
// (finish3d.hlsli: edge marking, then fog), composed with the 2D engine's own result as gpu2d.c does
// from its composition records, the master brightness last. In widescreen the DS's 256 columns start
// at `origin_x` (docs/RENDERING.md).

[[vk::image_format("r32ui")]] RWTexture2D<uint> color3d : register(u0, space1); // raster3d's, read only here
[[vk::image_format("r32ui")]] RWTexture2D<uint> attr3d : register(u1, space1);
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> screen : register(u2, space1);

ByteAddressBuffer records : register(t0, space0); // record_columns x 192, two words each, read by their bytes (raster3d.hlsl says why)

cbuffer Params : register(b0, space2) {
    uint scale;         // N
    uint width;         // the 3D target and the screen, pixels
    uint height;
    uint disp3dcnt;
    uint swap_params;
    uint clear_color;   // CLEAR_COLOR
    uint clear_depth;   // CLEAR_DEPTH
    uint fog_color;     // FOG_COLOR
    uint fog_offset;    // FOG_OFFSET
    uint eva, evb, evy; // the 2D engine's coefficients this frame
    uint master_bright; // MASTER_BRIGHT
    uint bg0_hofs;      // the 3D layer's horizontal scroll, DS pixels
    int origin_x;       // the DS column of the screen's first column (0 at 4:3)
    uint backdrop;      // engine A's backdrop colour (BGR555) | BLDCNT << 16
    int record_origin;  // the DS column of the records' first column
    uint record_columns; // how many columns the records have (256: the DS's)
    uint unused0, unused1;
    uint4 edge_colors;  // EDGE_COLOR, two entries per uint
    uint4 fog_table[2]; // FOG_TABLE, four entries per uint
};

int expand5(int c)
{
    return c != 0 ? c * 2 + 1 : 0;
}

uint edge_color(uint index)
{
    const uint word = edge_colors[index >> 1];
    return (index & 1u) != 0u ? word >> 16 : word & 0xffffu;
}

uint fog_density(uint index)
{
    return (fog_table[index >> 4][(index >> 2) & 3u] >> ((index & 3u) * 8u)) & 0x7fu;
}

// The 3D at a pixel of its target, edge-marked and fogged as gpu3d.c does:
// RGB 6 bits each in bits 0-5/8-13/16-21, alpha 0-31 in 24-28.
uint finished3d(int x, int y)
{
    const uint c = color3d[uint2(x, y)];
    const uint att = attr3d[uint2(x, y)];
    int r = (int)(c & 63u), g = (int)((c >> 8) & 63u), b = (int)((c >> 16) & 63u), a = (int)((c >> 24) & 31u);
    const uint translucent = c >> 31;
    const uint depth = att & 0xffffffu, id = (att >> 24) & 63u, edge = (att >> 30) & 1u, fog = att >> 31;
    if ((disp3dcnt & 0x20u) != 0u && edge != 0u && translucent == 0u) {
        const uint cd = clear_depth & 0x7fffu;
        const uint clear_d = cd * 0x200u + ((cd + 1u) / 0x8000u) * 0x1ffu;
        const uint clear_id = (clear_color >> 24) & 63u;
        const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            uint nid, nd;
            if (nx < 0 || nx >= (int)width || ny < 0 || ny >= (int)height) {
                nid = clear_id;
                nd = clear_d;
            } else {
                const uint natt = attr3d[uint2(nx, ny)];
                nid = (natt >> 24) & 63u;
                nd = natt & 0xffffffu;
            }
            if (nid != id && depth < nd) {
                const uint e = edge_color(id >> 3);
                r = expand5((int)(e & 31u));
                g = expand5((int)((e >> 5) & 31u));
                b = expand5((int)((e >> 10) & 31u));
                break;
            }
        }
    }
    if ((disp3dcnt & 0x80u) != 0u && fog != 0u) {
        const uint wbuffer = (swap_params >> 1) & 1u;
        const uint offset = fog_offset & 0x7fffu;
        const uint shift = (disp3dcnt >> 8) & 15u;
        const uint step = 0x400u >> shift;
        const int fr = expand5((int)(fog_color & 31u)), fg = expand5((int)((fog_color >> 5) & 31u)),
                  fb = expand5((int)((fog_color >> 10) & 31u));
        const int fa = (int)((fog_color >> 16) & 31u);
        const uint d = wbuffer != 0u ? depth : depth >> 9; // the 15-bit scale of FOG_OFFSET
        int density;
        if (step == 0u || d <= offset + step) {
            density = (int)fog_density(0u);
        } else if (d >= offset + step * 32u) {
            density = (int)fog_density(31u);
        } else {
            const uint n = (d - offset) / step - 1u;
            const uint lo = offset + step * (n + 1u);
            const int d0 = (int)fog_density(n), d1 = (int)fog_density(n + 1u);
            density = d0 + (d1 - d0) * (int)(d - lo) / (int)step;
        }
        if (density == 127) density = 128;
        if ((disp3dcnt & 0x40u) == 0u) {
            r = (fr * density + r * (128 - density)) / 128;
            g = (fg * density + g * (128 - density)) / 128;
            b = (fb * density + b * (128 - density)) / 128;
        }
        a = (fa * density + a * (128 - density)) / 128;
    }
    return (uint)r | ((uint)g << 8) | ((uint)b << 16) | ((uint)a << 24);
}

// ---- gpu2d.c's colour arithmetic, on BGR555 ----

uint channel(uint c, uint shift)
{
    return (c >> shift) & 31u;
}

uint blend(uint a, uint b, uint ea, uint eb)
{
    uint o = 0u;
    for (uint shift = 0u; shift < 15u; shift += 5u) {
        const uint v = (channel(a, shift) * ea + channel(b, shift) * eb) >> 4;
        o |= min(v, 31u) << shift;
    }
    return o;
}

uint brighten(uint a, uint ey)
{
    uint o = 0u;
    for (uint shift = 0u; shift < 15u; shift += 5u) {
        const uint v = channel(a, shift);
        o |= (v + (((31u - v) * ey) >> 4)) << shift;
    }
    return o;
}

uint darken(uint a, uint ey)
{
    uint o = 0u;
    for (uint shift = 0u; shift < 15u; shift += 5u) {
        const uint v = channel(a, shift);
        o |= (v - ((v * ey) >> 4)) << shift;
    }
    return o;
}

#define ROLE_HIDDEN 0u
#define ROLE_TOP 1u
#define EFFECT_BLEND 1u
#define EFFECT_BRIGHTEN 2u
#define EFFECT_DARKEN 3u

// khdays_gpu2d_recompose
uint recompose(uint2 rec, uint c3d, uint alpha3d)
{
    const uint under = rec.x & 0xffffu, other = rec.x >> 16;
    const uint role = rec.y & 0xffu, alpha_target = (rec.y >> 8) & 0xffu, effect = (rec.y >> 16) & 0xffu;
    if (alpha3d == 0u || role == ROLE_HIDDEN) {
        return under;
    }
    if (role == ROLE_TOP) {
        if (alpha3d < 31u && alpha_target != 0u) {
            return blend(c3d, other, alpha3d / 2u, 16u - alpha3d / 2u);
        }
        switch (effect) {
        case EFFECT_BLEND: return c3d; // by its own alpha, not BLDALPHA's: opaque
        case EFFECT_BRIGHTEN: return brighten(c3d, evy);
        case EFFECT_DARKEN: return darken(c3d, evy);
        default: return c3d;
        }
    }
    switch (effect) {
    case EFFECT_BLEND: return blend(other, c3d, eva, evb);
    case EFFECT_BRIGHTEN: return brighten(other, evy);
    case EFFECT_DARKEN: return darken(other, evy);
    default: return other;
    }
}

// khdays_gpu2d_finish: the master brightness on 6 bits, then 8 bits.
float4 finish(uint color)
{
    const uint factor = min(master_bright & 31u, 16u);
    const uint mode = (master_bright >> 14) & 3u;
    float4 o = float4(0.0f, 0.0f, 0.0f, 1.0f);
    for (uint n = 0u; n < 3u; ++n) {
        const uint c5 = channel(color, 5u * n);
        uint c = c5 != 0u ? c5 * 2u + 1u : 0u;
        if (mode == 1u) {
            c += (63u - c) * factor / 16u;
        } else if (mode == 2u) {
            c -= c * factor / 16u;
        }
        o[n] = (float)((c << 2) | (c >> 4)) / 255.0f;
    }
    return o;
}

// Beside the DS's columns: the record the 2D engine would give BG0 over the
// backdrop alone (gpu2d.c pick and effects, with no other layer).
uint2 beside()
{
    const uint color = backdrop & 0x7fffu, bldcnt = backdrop >> 16;
    const uint effect = (bldcnt >> 6) & 3u;
    const uint second = (bldcnt >> 13) & 1u;  // the backdrop a second target
    uint under = color, effect3d = 0u;
    if ((bldcnt & 0x20u) != 0u) {  // the backdrop a first target, when nothing is over it
        // gpu2d.c takes the backdrop as the second layer too
        under = effect == 1u && second != 0u ? blend(color, color, eva, evb)
              : effect == 2u ? brighten(color, evy) : effect == 3u ? darken(color, evy) : color;
    }
    if ((bldcnt & 1u) != 0u) {     // BG0 a first target
        effect3d = effect == 1u && second != 0u ? EFFECT_BLEND : effect == 2u ? EFFECT_BRIGHTEN
                 : effect == 3u ? EFFECT_DARKEN : 0u;
    }
    return uint2(under | (color << 16), ROLE_TOP | (second << 8) | (effect3d << 16));
}

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    uint c = 0u;
    int sx;
    const int column = (int)(id.x / scale) + origin_x; // the DS column
    // the 3D layer scrolls over 512 dots, the second 256 transparent: a
    // scroll from 256 on moves it back
    const int shift = bg0_hofs < 256u ? (int)bg0_hofs : (int)bg0_hofs - 512;
    if (id.x >= width || id.y >= height) {
        return;
    }
    sx = (int)id.x + shift * (int)scale;
    if (sx >= 0 && sx < (int)width) {
        c = finished3d(sx, (int)id.y);
    }
    {
        const uint c555 = ((c >> 1) & 31u) | (((c >> 9) & 31u) << 5) | (((c >> 17) & 31u) << 10);
        const int at = column - record_origin;
        const uint2 rec = at >= 0 && at < (int)record_columns ? records.Load2(((id.y / scale) * record_columns + (uint)at) * 8u)
                                                              : beside();
        screen[id.xy] = finish(recompose(rec, c555, (c >> 24) & 31u));
    }
}
