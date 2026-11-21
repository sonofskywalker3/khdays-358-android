// The clip volumes of a 3D list (gfx/g3d.h KHDAYS_G3D_CLIP, docs/RENDERING.md "Clip volumes") for the card's
// own rasterizer, before native3d.hlsl draws the polygons: each volume triangle drawn twice into two targets
// the polygons' shader then reads at its pixel (native3d_clip_at.hlsli),
//   0 R32_FLOAT  the nearest depth of the volumes' triangles covering the pixel (a whole number of 24 bits,
//                exact in a float), drawn with the depth test "less"
//   1 RGBA8      how many cover it, a 255th each (r), drawn adding, without the depth test
// as raster3d.hlsl keeps them while it goes through a pixel's polygons. A triangle covers a pixel more on each
// side than a drawn polygon does, as raster3d.hlsl's cover() grows a volume's: its corners are moved out from
// its edges (two triangles sharing an edge could leave a pixel to neither, and the room showed through).

#include "native3d_params.hlsli"

#if defined(STAGE_VS)
ByteAddressBuffer polys : register(t0, space0);
ByteAddressBuffer verts : register(t1, space0);
#include "g3d_list.hlsli"
#endif

static const float GROW = 1.0f; // target pixels, each side (raster3d.hlsl cover()'s grow)

struct Corner {
    float4 position : SV_Position;
    float w : TEXCOORD0;               // the DS's w, as native3d.hlsl's
    noperspective float z : TEXCOORD1; // z over w, linear over the screen
};

#if defined(STAGE_VS)
// A vertex's place in the target, pixels
float2 place(Vertex v)
{
    return float2((v.x - (float)origin_x) * (float)scale, v.y * (float)scale);
}

// The unit normal of the edge a to b, pointing away from `inside`
float2 outward(float2 a, float2 b, float2 inside)
{
    const float2 e = b - a;
    const float len = length(e);
    float2 n;
    if (len < 1e-6f) {
        return float2(0.0f, 0.0f);
    }
    n = float2(e.y, -e.x) / len;
    return dot(n, inside - a) > 0.0f ? -n : n;
}

// How `f` (its values at three of the polygon's corners, at `a`, `b` and `c` of the target, twice the area
// `area` between them) changes a pixel right and a pixel down: the polygon is flat, so anything linear over the
// screen follows its plane.
float2 slope(float2 a, float2 b, float2 c, float area, float3 f)
{
    const float2 ab = b - a, ac = c - a;
    return float2((f.y - f.x) * ac.y - (f.z - f.x) * ab.y, (f.z - f.x) * ab.x - (f.y - f.x) * ac.x) / area;
}

// A triangle's corner (`c`: a vertex, its polygon), moved GROW pixels out from the polygon's two edges that
// meet at it (no farther than 4 GROW at a sharp corner). Its depth goes on along the polygon's plane -- z over
// w and 1 / w are linear over the screen -- so that the pixels the polygon covered keep the depth they had; a
// corner whose plane cannot be told (it and its two neighbours in a line, a w of 0 or less) stays where it is.
Corner vs_main(uint2 c : TEXCOORD0)
{
    const Polygon p = polygon_at(c.y & 0x3fffffffu);
    const int k = (int)c.x - p.first;
    const Vertex v = vertex_at((int)c.x);
    const Vertex before = vertex_at(p.first + (k + p.count - 1) % p.count);
    const Vertex after = vertex_at(p.first + (k + 1) % p.count);
    const float2 from = place(v);
    float2 at = from, middle = float2(0.0f, 0.0f);
    float z = v.z, w = v.w;
    Corner o;
    for (int i = 0; i < p.count; ++i) {
        middle += place(vertex_at(p.first + i));
    }
    middle /= (float)max(p.count, 1);
    {
        const float2 n0 = outward(place(before), at, middle), n1 = outward(at, place(after), middle);
        const float2 sum = n0 + n1;
        const float facing = 1.0f + dot(n0, n1);
        float2 move = facing > 0.125f ? sum / facing : sum;
        const float far = length(move);
        if (far > 4.0f) {
            move *= 4.0f / far;
        }
        at += move * GROW;
    }
    {
        // the plane from the corner and its two neighbours
        const float2 a = place(before), d = place(after);
        const float area = (from.x - a.x) * (d.y - a.y) - (from.y - a.y) * (d.x - a.x);
        float iw = 0.0f;
        if (abs(area) >= 1e-3f && before.w > 0.0f && v.w > 0.0f && after.w > 0.0f) {
            iw = 1.0f / v.w +
                 dot(slope(a, from, d, area, float3(1.0f / before.w, 1.0f / v.w, 1.0f / after.w)), at - from);
        }
        if (iw > 1e-9f) {
            z += dot(slope(a, from, d, area, float3(before.z, v.z, after.z)), at - from);
            w = 1.0f / iw;
        } else {
            at = from;
        }
    }
    {
        // native3d.hlsl's clip space: the target's pixels to -1..1 (y up), w the DS's own
        const float depth = clamp(z * 0.5f + 8388096.0f / 16777216.0f, 0.0f, 1.0f);
        const float cx = at.x * 2.0f / (float)width - 1.0f, cy = 1.0f - at.y * 2.0f / (float)height;
        o.position = float4(cx * w, cy * w, depth * w, w);
    }
    o.w = w;
    o.z = z;
    return o;
}
#endif

// g3d_vram.hlsli's depth_of (that file reads the texture memory, which this pass does not have)
uint clip_depth_of(float z, float w, uint wbuffer)
{
    if (wbuffer != 0u) {
        return w < 0.0f ? 0u : w > 16777215.0f ? 0xffffffu : (uint)w;
    }
    {
        const float scaled = floor(z * 8388608.0f);
        if (scaled < -8388096.0f) return 0u;
        if (scaled > 8389119.0f) return 0xffffffu;
        return (uint)((int)scaled + 8388096);
    }
}

struct Targets {
    float near : SV_Target0;
    float4 count : SV_Target1;
    float depth : SV_Depth;
};

Targets ps_main(Corner i)
{
    const uint depth = clip_depth_of(i.z, i.w, (swap_params >> 1) & 1u);
    Targets o;
    o.near = (float)depth;
    o.count = float4(1.0f / 255.0f, 0.0f, 0.0f, 0.0f);
    o.depth = (float)depth / 16777216.0f;
    return o;
}
