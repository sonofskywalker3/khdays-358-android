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
// the DS-sized pass's verdicts (pre_mode): per DS pixel, at column 2x what it is (PRE_*), at 2x + 1 its colour
[[vk::image_format("r32ui")]] RWTexture2D<uint> pre : register(u6, space1);

// 32 words, 128 bytes, and no more: an Adreno 619 (driver 512.762.40) gave this shader nothing of its block past
// them. When it was 36 words, row0 and the three after it read as 0 there, or as what layers2d.hlsl's pass had
// left behind when that ran just before (the title's art in three bands, the one-screen picture without its
// menus); a block of 32 is read whole (measured 2026-11-10). So the small ones share a word (`modes`).
cbuffer Params : register(b0, space2) {
    uint scale;         // N
    uint width;         // the screen and raster3d's targets, pixels
    uint height;
    uint modes;         // source3d, smooth3d, hud3d and pre_mode (below)
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
    uint hud_width;     // the HUD's 3D apart: its targets, texels (as wide as the 3D's at N)
    uint hud_height;
    uint row0;          // the DS row of the target's first (rows past the DS's 192 for a taller display)
    uint layers_off;    // layers left out (KDF_BG_ON_SHIFT's bits, KDF_OBJ_ON): compose.h KD_LAYERS_OFF
    uint layers1x_rows; // layers1x: each layer's rows from row0 (0: none; the layers looked up here)
};
// `modes` (gfx_render.c compose_params packs them):
#define source3d (modes & 3u)         // 0 none, 1 layer3d_mem at 1x, 2 raster3d's targets at N x
#define smooth3d ((modes >> 2) & 1u)  // 1: the 3D read between its texels (smooth scaling)
#define hud3d ((modes >> 3) & 1u)     // 1: the HUD's 3D apart, over the rest
#define pre_mode ((modes >> 4) & 3u)  // 0: each pixel composed here; 1: this is the DS-sized pass writing `pre`; 2: `pre` read

#define FINISH3D_SCALED
#include "finish3d.hlsli"

static uint g_fx, g_fy; // the output pixel's place inside its DS pixel
static bool g_place_used; // KD_PLACE_USED said: a replacement's or an element's texel decides the colour
static bool g_3d_used;    // the 3D was read
static bool g_only3d;       // PRE_3D_ONLY: kd_pixel looks up the 3D's layer alone (KD_STACK_FLAGS)
static bool g_stack_3d_only; // KD_STACK_SEEN: only the 3D lay at the DS pixel

#define PRE_EACH 0u   // composed at each output pixel
#define PRE_SAME 1u   // the same at every place in the DS pixel: its colour composed once
#define PRE_3D_ONLY 2u // only the 3D there (no layer over it, no sprite, no element): the 3D composed alone

static uint g_3d_x, g_3d_value; // the 3D read once before the composition (read3d_once): its column, its value
static int g_3d_row;

// The 3D at (x, row) as its source holds it. Called once in the shader (read3d_once): it is inlined wherever it
// is called, several finished texels each time, and compose.h asks for the 3D in five places.
uint layer3d_read(uint x, int row)
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

// compose.h's KD_3D: the value read3d_once read, the only place the composition asks for (the DS pixel's 3D)
uint layer3d_at(uint x, int row)
{
    g_3d_used = true;
    return x == g_3d_x && row == g_3d_row ? g_3d_value : 0u;
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
// used for the frames its name says, so what it leaves out is not reached. The split part keeps the split
// as the description says, as the whole shader does: with it fixed on, an Adreno 619 (driver 512.762) drew
// the HUD's pieces of the one-screen picture as white boxes.
#if defined(COMPOSE2D_PART_PLAIN)
#define KD_SPLIT_ON(flags) false
#endif

#define KD_PLACE_USED() (g_place_used = true)
// PRE_3D_ONLY read: the 3D's layer alone looked up
#define KD_STACK_FLAGS(flags) \
    (g_only3d ? (flags) & ~(KDF_OBJ_ON | (14u << KDF_BG_ON_SHIFT)) : (flags))
// the pass at the DS's size: whether only the 3D lay there, with no replacement or element read for it
#define KD_STACK_SEEN(s) \
    (g_stack_3d_only = !g_place_used && (s).color[1] == 0u && (s).color[2] == 0u && (s).color[3] == 0u && \
                       (s).spr.color == 0u && (s).spr.window == 0u)
#include "../gfx/compose.h"

float channel8(uint c6)
{
    return (float)((c6 << 2) | (c6 >> 4)) / 255.0f; // as to_rgb888
}

// The DS pixel composed whole, as this shader's part does it.
uint pixel_whole(int x, int row)
{
#if defined(COMPOSE2D_PART_WIDE)
    return kd_pixel_wide(x, row);
#elif defined(COMPOSE2D_PART_PLAIN) || defined(COMPOSE2D_PART_SPLIT)
    return kd_pixel(x, row);
#else
    return KD_DESC(KD_WIDE) != 0u ? kd_pixel_wide(x, row) : kd_pixel(x, row);
#endif
}

// The 3D the composition of (x, row) asks for, read before it: the column kd_layer, kd_alpha3d, kd_fine3d and
// kd_moved3d ask for, or kd_pixel_wide's in its wider target. Read only where it can be asked for.
void read3d_once(int x, int row)
{
#if defined(COMPOSE2D_PART_WIDE)
    const bool wide = true;
#elif defined(COMPOSE2D_PART_PLAIN) || defined(COMPOSE2D_PART_SPLIT)
    const bool wide = false;
#else
    const bool wide = KD_DESC(KD_WIDE) != 0u;
#endif
    uint column;
    bool inside;
    if (wide) {
        const uint hofs = KD_DESC(KD_HOFS3D);
        const int target = x + (hofs < 256u ? (int)hofs : (int)hofs - 512);
        column = (uint)target;
        inside = target >= 0 && target < 256 + 2 * (int)KD_DESC(KD_WIDE);
    } else {
        column = ((uint)x + KD_DESC(KD_HOFS3D)) & 511u;
        inside = column < 256u;
    }
    g_3d_x = column;
    g_3d_row = row;
    g_3d_value = inside && source3d != 0u ? layer3d_read(column, row) : 0u;
}

// The composition is called once, below, whatever pre_mode is: each call of it is the whole of compose.h inlined,
// and Mali's compiler runs out of memory on a shader with more than one.
[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    uint c6, verdict = PRE_EACH;
    int x, row;
    if (pre_mode == 1u) {
        // the pass at the DS's size: one thread a DS pixel, its colour at the first place in it
        if (id.x >= width / scale || id.y >= height / scale) {
            return;
        }
        g_fx = 0u;
        g_fy = 0u;
        x = (int)id.x;
        row = (int)id.y + (int)row0;
    } else {
        if (id.x >= width || id.y >= height) {
            return;
        }
        g_fx = id.x % scale;
        g_fy = id.y % scale;
        x = (int)(id.x / scale);
        row = (int)(id.y / scale) + (int)row0;
        if (pre_mode == 2u) {
            verdict = pre[uint2((uint)x * 2u, id.y / scale)];
        }
    }
    g_place_used = false;
    g_3d_used = false;
    g_stack_3d_only = false;
    g_only3d = verdict == PRE_3D_ONLY;
    if (verdict == PRE_SAME) {
        c6 = pre[uint2((uint)x * 2u + 1u, id.y / scale)];
    } else {
        read3d_once(x, row);
        c6 = pixel_whole(x, row);
    }
    if (pre_mode == 1u) {
        // which of the DS pixel's output pixels can skip the composition: all of them when no place inside it
        // decided the colour (PRE_SAME), or have only the 3D looked up when it lay there alone (PRE_3D_ONLY)
        const uint flags = KD_DESC(KD_FLAGS) & ~(uint)(KD_LAYERS_OFF);
        const bool alone3d = g_stack_3d_only && KD_DESC(KD_WIDE) == 0u && (flags & KDF_DISPLAY_MASK) == 1u &&
                             (flags & KDF_FORCED_BLANK) == 0u && !KD_SPLIT_ON(flags) &&
                             (flags & KDF_BG0_IS_3D) != 0u && (flags & (1u << KDF_BG_ON_SHIFT)) != 0u;
        verdict = !g_place_used && !g_3d_used ? PRE_SAME : alone3d ? PRE_3D_ONLY : PRE_EACH;
        pre[uint2(id.x * 2u, id.y)] = verdict;
        pre[uint2(id.x * 2u + 1u, id.y)] = c6;
        return;
    }
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
