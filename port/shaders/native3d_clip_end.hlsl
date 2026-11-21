// The native 3D renderer's clip volumes, after native3d_pack.hlsl made raster3d.hlsl's targets (raster3d.hlsl
// cs_main's end): where a volume shows and no polygon wrote a depth -- the pixel still holds the rear plane's --
// the coat's inside is the surface, at the volume's nearest depth, with the polygon ID and fog of the owner's
// polygon drawn last on the pixel (native3d.hlsl's flags; the rear plane's stay where none was). Only for
// a list the native 3D draws whole: when raster3d.hlsl draws the rest it applies the rule itself, after its own
// polygons (gpu_native3d.c).

Texture2D<float> clip_near : register(t0, space0);   // native3d_clip.hlsl's targets
Texture2D<float4> clip_count : register(t1, space0);
[[vk::image_format("r32ui")]] RWTexture2D<uint> out_attr : register(u0, space1);
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> flags : register(u1, space1); // read only here

#include "native3d_params.hlsli"

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    const int3 texel = int3((int)id.x, (int)id.y, 0);
    const uint cd = clear_depth & 0x7fffu;
    const uint rear = cd * 0x200u + ((cd + 1u) / 0x8000u) * 0x1ffu; // g3d_vram.hlsli rear_plane_at's, no clear image
    if (id.x >= width || id.y >= height) {
        return;
    }
    if (clip_count.Load(texel).r < 1.5f / 255.0f) {
        return;
    }
    {
        const float near = clip_near.Load(texel);
        const uint attr = out_attr[id.xy];
        if ((attr & 0xffffffu) != rear || (float)rear <= near) {
            return;
        }
        {
            const uint mark = (uint)round(flags[id.xy].b * 255.0f);
            const uint owner = mark != 0u ? (mark - 1u) & 63u : (attr >> 24) & 63u;
            const uint owner_fog = mark != 0u ? (mark - 1u) >> 6 : 1u;
            out_attr[id.xy] = (uint)near | (owner << 24) | (attr & 0x40000000u) | (((attr >> 31) & owner_fog) << 31);
        }
    }
}
