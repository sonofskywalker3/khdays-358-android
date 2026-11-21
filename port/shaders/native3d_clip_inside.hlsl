// The native 3D renderer's clip volumes, before the list's polygons (raster3d.hlsl lay_clip_inside): where a
// volume shows before the rear plane the pixel starts as the coat's inside, black and opaque with the fog bit,
// under everything drawn after. One triangle over the whole target (native3d_rear.hlsl's vs_main), tested at
// the volume's nearest depth against the rear plane's, which stays for the coat to draw on: the depth, the
// stencil and the polygon IDs are not written (gpu_native3d.c clip_inside).

#include "native3d_params.hlsli"
#include "native3d_clip_at.hlsli"

struct Targets {
    float4 colour : SV_Target0;
    float4 first : SV_Target1;
    float4 fog : SV_Target2;
    float4 opaque : SV_Target3;
    float4 flags : SV_Target4;
    float depth24 : SV_Target5;
    float depth : SV_Depth;
};

Targets ps_main(float4 at : SV_Position)
{
    Targets o = (Targets)0;
    float near;
    if (!clip_volume_at(at.xy, near)) {
        discard;
    }
    o.depth = near / 16777216.0f;
    // as an opaque pixel of native3d.hlsl's: black, alpha 31, the fog bit taken
    o.colour = float4(0.0f, 0.0f, 0.0f, 1.0f);
    o.first = float4(0.0f, 0.0f, 0.0f, 1.0f);
    o.fog = float4(1.0f, 0.0f, 0.0f, 1.0f);
    o.flags = float4(0.0f, 0.0f, 0.0f, 1.0f);
    return o;
}
