// Diagnostic: compose2d.hlsl's parameter block, each of its 36 words written to the picture's first row.
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> out_screen : register(u2, space1);

cbuffer Params : register(b0, space2) {
    uint scale; uint width; uint height; uint source3d; uint disp3dcnt; uint swap_params; uint clear_color; uint clear_depth;
    uint fog_color; uint fog_offset; uint fine; uint offset3d;
    uint4 edge_colors;
    uint4 fog_table[2];
    uint scale3d; uint width3d; uint height3d; uint smooth3d;
    uint hud3d; uint hud_width; uint hud_height; uint unused5;
    uint row0; uint layers_off; uint layers1x_rows; uint pre_mode;
};

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    uint v[36] = {scale, width, height, source3d, disp3dcnt, swap_params, clear_color, clear_depth,
                  fog_color, fog_offset, fine, offset3d,
                  edge_colors.x, edge_colors.y, edge_colors.z, edge_colors.w,
                  fog_table[0].x, fog_table[0].y, fog_table[0].z, fog_table[0].w,
                  fog_table[1].x, fog_table[1].y, fog_table[1].z, fog_table[1].w,
                  scale3d, width3d, height3d, smooth3d, hud3d, hud_width, hud_height, unused5,
                  row0, layers_off, layers1x_rows, pre_mode};
    if (id.y != 0u || id.x >= 36u) {
        return;
    }
    const uint c = v[id.x];
    out_screen[id.xy] = float4((float)(c & 255u), (float)((c >> 8) & 255u), (float)((c >> 16) & 255u), 255.0f) / 255.0f;
}
