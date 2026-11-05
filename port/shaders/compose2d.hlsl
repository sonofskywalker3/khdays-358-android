// The port's own 2D: one screen at N times the DS's resolution, each pixel composed from its frame
// description by port/gfx/compose.h (the code the CPU runs too): the 2D of the DS pixel it lies in,
// the 3D of its own place. In widescreen engine A's screen is KD_WIDE columns wider each side.

#include "../gfx/frame.h"

// Eight buffers, the most SDL's GPU API binds to a pass, read by their bytes (raster3d.hlsl says why):
ByteAddressBuffer desc : register(t0, space0);       // KHDAYS_DESC_WORDS, then the palette: the layers' 256 colours, the sprites'
ByteAddressBuffer bg_mem : register(t1, space0);     // the layers' memory, then its map of tiles drawn instead (runtime/mods/tiles2d.h)
ByteAddressBuffer obj_mem : register(t2, space0);    // the sprites', then theirs
ByteAddressBuffer bgext_mem : register(t3, space0);  // the extended palettes
ByteAddressBuffer objext_mem : register(t4, space0);
ByteAddressBuffer vram_mem : register(t5, space0);   // the bank a VRAM display shows
ByteAddressBuffer layer3d_mem : register(t6, space0); // the 3D at 1x (256x192), for source3d 1
ByteAddressBuffer hd : register(t7, space0);         // the replacements (host/render/gpu_hd.h)

[[vk::image_format("r32ui")]] RWTexture2D<uint> color3d : register(u0, space1); // raster3d's, for source3d 2
[[vk::image_format("r32ui")]] RWTexture2D<uint> attr3d : register(u1, space1);
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> out_screen : register(u2, space1);
// the HUD's 3D drawn apart at N (gpu_raster3d.h), when the 3D is drawn coarser than the 2D
[[vk::image_format("r32ui")]] RWTexture2D<uint> hud_color3d : register(u3, space1);
[[vk::image_format("r32ui")]] RWTexture2D<uint> hud_attr3d : register(u4, space1);
[[vk::image_format("r32ui")]] RWTexture2D<uint> layers1x : register(u5, space1); // layers2d.hlsl's, read only

cbuffer Params : register(b0, space2) {
    uint scale;         // N
    uint width;         // the screen and raster3d's targets, pixels
    uint height;
    uint source3d;      // 0 none, 1 layer3d_mem at 1x, 2 raster3d's targets at N x
    uint disp3dcnt;
    uint swap_params;
    uint clear_color;   // CLEAR_COLOR
    uint clear_depth;   // CLEAR_DEPTH
    uint fog_color;     // FOG_COLOR
    uint fog_offset;    // FOG_OFFSET
    uint fine;          // 1: the 3D's channels have 8 bits, and the screen is composed in 8 (KDF_FINE)
    uint offset3d;      // source3d 2: the 3D's columns left of the DS's (the one picture's, as wide as it)
    uint4 edge_colors;  // EDGE_COLOR, two entries per uint
    uint4 fog_table[2]; // FOG_TABLE, four entries per uint
    uint scale3d;       // source3d 2: the 3D's N (`scale` but when the 2D is drawn finer than the 3D)
    uint width3d;       // its targets, texels
    uint height3d;
    uint smooth3d;      // 1: the 3D read between its texels (smooth scaling)
    uint hud3d;         // 1: the HUD's 3D apart, over the rest
    uint hud_width;     // its targets, texels (as wide as the 3D's at N)
    uint hud_height;
    uint unused5;
    uint row0;          // the DS row of the target's first (rows past the DS's 192 for a taller display)
    uint layers_off;    // layers left out (KDF_BG_ON_SHIFT's bits, KDF_OBJ_ON): compose.h KD_LAYERS_OFF
    uint layers1x_rows; // layers1x: each layer's rows from row0 (0: none; the layers looked up here)
    uint pad;
};

#define FINISH3D_SCALED
#include "finish3d.hlsli"

static uint g_fx, g_fy; // the output pixel's place inside its DS pixel

uint layer3d_at(uint x, int row)
{
    if (source3d == 1u) {
        return layer3d_mem.Load(((uint)row * 256u + x) * 4u);
    }
    if (source3d == 2u && scale3d == scale) {
        return finished3d((int)((x + offset3d) * scale + g_fx), (int)((uint)row * scale + g_fy));
    }
    if (source3d == 2u) {
        // the 2D finer than the 3D: the 3D at this output pixel's middle, the HUD's drawn apart over it
        const uint world =
            finished3d_at(((float)(x + offset3d) + ((float)g_fx + 0.5f) / (float)scale) * (float)scale3d,
                          ((float)row + ((float)g_fy + 0.5f) / (float)scale) * (float)scale3d);
        if (hud3d == 0u) {
            return world;
        }
        return over3d(finish3d_texel(hud_color3d, hud_attr3d, (int)hud_width, (int)hud_height,
                                     (int)((x + offset3d) * scale + g_fx), (int)((uint)row * scale + g_fy)),
                      world);
    }
    return 0u;
}

#define KD_LAYERS_OFF layers_off
#define KD_FN
#define KD_VOID
#define KD_DESC(i) desc.Load((i) * 4u)
#define KD_BG8(a) ((bg_mem.Load(((a) >> 2) * 4u) >> (((a) & 3u) * 8u)) & 0xffu)
#define KD_BG16(a) ((bg_mem.Load(((a) >> 2) * 4u) >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_OBJ8(a) ((obj_mem.Load(((a) >> 2) * 4u) >> (((a) & 3u) * 8u)) & 0xffu)
#define KD_OBJ16(a) ((obj_mem.Load(((a) >> 2) * 4u) >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_BGEXT16(a) ((bgext_mem.Load(((a) >> 2) * 4u) >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_OBJEXT16(a) ((objext_mem.Load(((a) >> 2) * 4u) >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_PAL16(i) ((desc.Load((KHDAYS_DESC_WORDS + ((i) >> 1)) * 4u) >> (((i) & 1u) * 16u)) & 0xffffu)
#define KD_VRAM16(a) ((vram_mem.Load(((a) >> 2) * 4u) >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_3D(x, row) layer3d_at((x), (row))
#define KD_3D_KNOWN (source3d != 0u)
// the maps after the memories: engine A's layers' 512 KB and sprites' 256 KB, engine B's 128 KB each
#define KD_HD_BG(u) bg_mem.Load((((KD_DESC(KD_FLAGS) & KDF_ENGINE_B) != 0u ? 0x8000u : 0x20000u) + (u)) * 4u)
#define KD_HD_OBJ(u) obj_mem.Load((((KD_DESC(KD_FLAGS) & KDF_ENGINE_B) != 0u ? 0x8000u : 0x10000u) + (u)) * 4u)
#define KD_HD_FIRST(id) hd.Load((id) * 16u)
#define KD_HD_WIDTH(id) hd.Load(((id) * 4u + 1u) * 4u)
#define KD_HD_HEIGHT(id) hd.Load(((id) * 4u + 2u) * 4u)
#define KD_HD_INFO(id) hd.Load(((id) * 4u + 3u) * 4u)
#define KD_HD_TEXEL(i) hd.Load((i) * 4u)
#define KD_SUB_X (((float)g_fx + 0.5f) / (float)scale)
#define KD_SUB_Y (((float)g_fy + 0.5f) / (float)scale)
#define KD_PIXEL (1.0f / (float)scale) // an output pixel's size, in DS pixels
// the port's elements (gfx/elements.h): after the description and its palettes; their images are
// replacements (host/render/gpu_hd.h), their texels where the replacement's table puts them
#define KD_ELEMENT(i) desc.Load((KHDAYS_DESC_WORDS + 256u + (i)) * 4u)
#define KD_EL_TEXEL(image, i) hd.Load((hd.Load((image) * 16u) + (i)) * 4u)
#define KD_ASFLOAT(u) asfloat(u)
#define KD_LEFT_AT (KHDAYS_DESC_WORDS + 256u + KD_DESC(KD_ELEMENTS) * KE_WORDS)
#define KD_LEFT_OUT_CELL(c) (KD_DESC(KD_LEFT_OUT) != 0u ? (desc.Load((KD_LEFT_AT + ((c) >> 5)) * 4u) >> ((c) & 31u)) & 1u : 0u)
#define KD_LEFT_OUT_OBJ(i) (KD_DESC(KD_LEFT_OUT) != 0u ? (desc.Load((KD_LEFT_AT + KE_LEFT_CELL_WORDS + ((i) >> 5)) * 4u) >> ((i) & 31u)) & 1u : 0u)
// after them each row's sprites (gfx_render.c sprite_rows): 4 words a row, of 256
#define KD_ROWS_AT (KD_LEFT_AT + (KD_DESC(KD_LEFT_OUT) != 0u ? KE_LEFT_CELL_WORDS + KE_LEFT_OBJ_WORDS : 0u))
// A layer's colour at a DS pixel from layers2d.hlsl's pass where it has one (LAYER_LIVE: looked up here,
// as its replacement's texel depends on the place inside the DS pixel, or it is the 3D)
#define LAYER_LIVE 0xffffffffu
uint kd_layer(int b, int x, int row); // compose.h, included below
uint layer_at(int b, int x, int row)
{
    if (layers1x_rows != 0u && x >= 0 && x < 256 && row >= (int)row0 && row < (int)(row0 + layers1x_rows)) {
        const uint c = layers1x[uint2((uint)x, (uint)b * layers1x_rows + (uint)row - row0)];
        if (c != LAYER_LIVE) {
            return c;
        }
    }
    return kd_layer(b, x, row);
}
#define KD_LAYER_AT(b, x, row) layer_at((b), (x), (row))
#define KD_SPRITE_ROW(row, word) desc.Load((KD_ROWS_AT + ((uint)(row) & 255u) * 4u + (word)) * 4u)
#define KD_LOW_BIT(m) firstbitlow(m)

// A part of this shader for a compiler that runs out of memory on the whole of it (gfx_render.c compose_for):
// the wider picture's, or the DS's columns' with or without the one-screen picture's sources. Each is only
// used for the frames its name says, so what it leaves out is not reached.
#if defined(COMPOSE2D_PART_PLAIN)
#define KD_SPLIT_ON(flags) false
#elif defined(COMPOSE2D_PART_SPLIT)
#define KD_SPLIT_ON(flags) true
#endif

#include "../gfx/compose.h"

float channel8(uint c6)
{
    return (float)((c6 << 2) | (c6 >> 4)) / 255.0f; // as to_rgb888
}

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    uint c6;
    if (id.x >= width || id.y >= height) {
        return;
    }
    g_fx = id.x % scale;
    g_fy = id.y % scale;
#if defined(COMPOSE2D_PART_WIDE)
    c6 = kd_pixel_wide((int)(id.x / scale), (int)(id.y / scale) + (int)row0);
#elif defined(COMPOSE2D_PART_PLAIN) || defined(COMPOSE2D_PART_SPLIT)
    c6 = kd_pixel((int)(id.x / scale), (int)(id.y / scale) + (int)row0);
#else
    c6 = KD_DESC(KD_WIDE) != 0u ? kd_pixel_wide((int)(id.x / scale), (int)(id.y / scale) + (int)row0)
                                 : kd_pixel((int)(id.x / scale), (int)(id.y / scale) + (int)row0);
#endif
    if ((KD_DESC(KD_FLAGS) & KDF_SPLIT) != 0u) {
        // the one-screen picture's source: K in 8 bits, its alpha 0 (not shown over
        // the 3D) or 1 + the 3D's factor x 254 (kd_split)
        out_screen[id.xy] = float4((float)(c6 & 255u) / 255.0f, (float)((c6 >> 8) & 255u) / 255.0f,
                                   (float)((c6 >> 16) & 255u) / 255.0f, (float)(c6 >> 24) / 255.0f);
        return;
    }
    if ((KD_DESC(KD_FLAGS) & KDF_FINE) != 0u) {
        out_screen[id.xy] = float4((float)(c6 & 255u) / 255.0f, (float)((c6 >> 8) & 255u) / 255.0f,
                                   (float)((c6 >> 16) & 255u) / 255.0f, 1.0f);
        return;
    }
    out_screen[id.xy] = float4(channel8(c6 & 63u), channel8((c6 >> 8) & 63u), channel8((c6 >> 16) & 63u), 1.0f);
}
