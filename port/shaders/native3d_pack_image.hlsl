// After native3d_pack.hlsl, with the clear image on (DISP3DCNT bit 14): the colour of each pixel nothing was
// drawn on (alpha 0) from the clear image (rear_plane_at), as raster3d.hlsl's rear plane has it. A pass of its
// own, as small as can be: reading the image in native3d_pack.hlsl left newer Adreno drivers writing nothing
// from that shader at all. Only the colour changes: the alpha, depth and fog are the targets' already.

ByteAddressBuffer vram : register(t0, space0);

[[vk::image_format("r32ui")]] RWTexture2D<uint> out_color : register(u0, space1);

#include "native3d_params.hlsli"
#include "g3d_vram.hlsli"

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) {
        return;
    }
    const uint c = out_color[id.xy];
    if (((c >> 24) & 31u) != 0u) {
        return;
    }
    const int3 rgb = rear_plane_at((int)id.x / (int)scale + origin_x, id.y / scale).color.xyz;
    out_color[id.xy] = (uint)rgb.x | ((uint)rgb.y << 8) | ((uint)rgb.z << 16) | (c & 0xff000000u);
}
