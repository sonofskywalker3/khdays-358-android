// The clip volumes at a pixel, as native3d_clip.hlsl drew them before the polygons, for the shaders of the
// native 3D's pass that read them (native3d.hlsl, native3d_clip_inside.hlsl, native3d_clip_end.hlsl). The
// includer's constant buffer is native3d_params.hlsli's: `outputs` says whether the list has volumes (bit 8).

static const uint OUT_CLIPS = 0x100u;

#if defined(STAGE_VS)
// (a vertex stage of a file with both: its pixel stage's functions are compiled, never called)
bool clip_volume_at(float2 at, out float near)
{
    near = 16777216.0f;
    return false;
}
#else
Texture2D<float> clip_near : register(t0, space2);   // the nearest depth of the volumes' triangles here
Texture2D<float4> clip_count : register(t1, space2); // how many cover the pixel, a 255th each (r)

// The nearest depth of the clip volumes showing at target pixel `at`, or none: where two or more of their
// triangles cover it (a closed volume's front and back), as raster3d.hlsl counts them.
bool clip_volume_at(float2 at, out float near)
{
    const int3 texel = int3((int)at.x, (int)at.y, 0);
    near = 16777216.0f;
    if ((outputs & OUT_CLIPS) == 0u) {
        return false;
    }
    if (clip_count.Load(texel).r < 1.5f / 255.0f) {
        return false;
    }
    near = clip_near.Load(texel);
    return true;
}
#endif
