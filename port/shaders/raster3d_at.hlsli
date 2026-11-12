// The 3D (raster3d's targets, source3d 2) at DS column x and row, output texel (fx, fy) inside the DS pixel, as
// compose2d.hlsl's composition reads it: finished (finish3d.hlsli, included before with FINISH3D_SCALED), at its
// own scale, the HUD's 3D drawn apart over it. Needs compose2d_params.hlsli's block and the 3D's four targets.
// spread2d.hlsl reads it the same way.
uint raster3d_at(uint x, int row, uint fx, uint fy)
{
    if (scale3d == scale) {
        return finished3d((int)((x + offset3d) * scale + fx), (int)((uint)row * scale + fy));
    }
    // the 2D finer than the 3D: the 3D at this output pixel's middle, the HUD's drawn apart over it
    const uint world = finished3d_at(((float)(x + offset3d) + ((float)fx + 0.5f) / (float)scale) * (float)scale3d,
                                     ((float)row + ((float)fy + 0.5f) / (float)scale) * (float)scale3d);
    if (hud3d == 0u) {
        return world;
    }
    return over3d(finish3d_texel(hud_color3d, hud_attr3d, (int)hud_width, (int)hud_height,
                                 (int)((x + offset3d) * scale + fx), (int)((uint)row * scale + fy)),
                  world);
}
