// The port's own 2D: one screen at N times the DS's resolution, each pixel composed from its frame
// description by port/gfx/compose.h (the code the CPU runs too): the 2D of the DS pixel it lies in,
// the 3D of its own place. In widescreen engine A's screen is KD_WIDE columns wider each side.

#include "../gfx/frame.h"
#include "../gfx/spread2d.h"

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
// the DS-sized pass's verdicts (pre_mode): per DS pixel, at column 2x its verdict word, at 2x + 1 its colour or its
// elements (gfx/spread2d.h); below them spread2d.hlsl's list
[[vk::image_format("r32ui")]] RWTexture2D<uint> pre : register(u6, space1);

#include "compose2d_params.hlsli"
// the DS pixel the dispatch starts at, in eighths (only part of the screen composed: gfx_render.c one_reads)
#define origin_x (((modes >> 8) & 0xfffu) * 8u)
#define origin_y ((modes >> 20) * 8u)

#define FINISH3D_SCALED
#include "finish3d.hlsli"
#include "raster3d_at.hlsli"

static uint g_fx, g_fy; // the output pixel's place inside its DS pixel
static bool g_place_used; // KD_PLACE_USED said: a replacement's or an element's texel decides the colour
static bool g_3d_used;    // the 3D was read
static bool g_only3d;       // PRE_3D_ONLY: kd_pixel looks up the 3D's layer alone (KD_STACK_FLAGS)
static bool g_stack_3d_only; // KD_STACK_SEEN: only the 3D lay at the DS pixel

// the DS-sized pass's verdicts (PRE_*): gfx/spread2d.h

static uint g_3d_x, g_3d_value; // the 3D read once before the composition (read3d_once): its column, its value
static int g_3d_row;

// The 3D at (x, row) as its source holds it. Called once in the shader (read3d_once): it is inlined wherever it
// is called, several finished texels each time, and compose.h asks for the 3D in five places.
uint layer3d_read(uint x, int row)
{
    if (source3d == 1u) {
        return layer3d_mem.Load(((uint)row * 256u + x) * 4u);
    }
    if (source3d == 2u) {
        return raster3d_at(x, row, g_fx, g_fy);
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
// the elements (the first 31) left out of the DS pixel: transparent all over it (element_empty_at, in the DS-sized
// pass; read back from it in pre_mode 2)
static uint g_el_skip;
#define KD_ELEMENT_SKIP(e) ((e) < PRE_ELEMENTS_MOST && ((g_el_skip >> (e)) & 1u) != 0u)
// PRE_3D_ONLY read: the 3D's layer alone looked up
#define KD_STACK_FLAGS(flags) \
    (g_only3d ? (flags) & ~(KDF_OBJ_ON | (14u << KDF_BG_ON_SHIFT)) : (flags))
// the pass at the DS's size: whether only the 3D lay there, with no replacement or element read for it
#define KD_STACK_SEEN(s) \
    (g_stack_3d_only = !g_place_used && (s).color[1] == 0u && (s).color[2] == 0u && (s).color[3] == 0u && \
                       (s).spr.color == 0u && (s).spr.window == 0u)
#include "../gfx/compose.h"

// Whether element e gives no texel anywhere in DS pixel (x, row) -- x in its DS columns (the wider picture's minus its
// margin) -- as kd_element_texel takes them: every texel of its image the places inside the pixel can fall on is
// transparent (a few more at the far edge: never fewer). An element whose texels there are too many to look at
// (a picture drawn much smaller) counts as showing.
#define ELEMENT_LOOK_MOST 256
bool element_empty_at(uint e, int x, int row)
{
    const uint base = e * KE_WORDS;
    const float ex = KD_ASFLOAT(KD_ELEMENT(base + KE_X)), ey = KD_ASFLOAT(KD_ELEMENT(base + KE_Y));
    const float ew = KD_ASFLOAT(KD_ELEMENT(base + KE_W)), eh = KD_ASFLOAT(KD_ELEMENT(base + KE_H));
    const uint iw = KD_ELEMENT(base + KE_IW), ih = KD_ELEMENT(base + KE_IH), slot = KD_ELEMENT(base + KE_SLOT);
    const uint image = KD_ELEMENT(base + KE_IMAGE);
    if ((slot & KES_OVERLAY) != 0u || ((KD_DESC(KD_FLAGS) & KDF_SPLIT) != 0u && (slot & KES_PICTURE) != 0u)) {
        return true; // never drawn in the composition (kd_element_texel)
    }
    if (iw == 0u || ih == 0u || (float)x + 1.0f <= ex || (float)row + 1.0f <= ey || (float)x >= ex + ew ||
        (float)row >= ey + eh) {
        return true; // outside: kd_element_texel gives nothing there anyway
    }
    const float x0 = max((float)x, ex), x1 = min((float)x + 1.0f, ex + ew);
    const float y0 = max((float)row, ey), y1 = min((float)row + 1.0f, ey + eh);
    const int u0 = clamp((int)((x0 - ex) * (float)iw / ew), 0, (int)iw - 1);
    const int u1 = clamp((int)((x1 - ex) * (float)iw / ew), 0, (int)iw - 1);
    const int v0 = clamp((int)((y0 - ey) * (float)ih / eh), 0, (int)ih - 1);
    const int v1 = clamp((int)((y1 - ey) * (float)ih / eh), 0, (int)ih - 1);
    if ((u1 - u0 + 1) * (v1 - v0 + 1) > ELEMENT_LOOK_MOST) {
        return false;
    }
    for (int v = v0; v <= v1; ++v) {
        for (int u = u0; u <= u1; ++u) {
            const uint t = KD_EL_TEXEL(image, (uint)v * iw + (uint)u);
            if ((slot & KES_DIRECT) != 0u ? (t >> 24) != 0u : (t & KET_OPAQUE) != 0u) {
                return false;
            }
        }
    }
    return true;
}

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
void cs_main(uint3 dispatched : SV_DispatchThreadID, uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint c6, verdict = PRE_EACH;
    int x, row;
    // the dispatch covers the part of the screen from `origin` (DS pixels; output pixels N x that)
    uint2 id = dispatched.xy + ((pre_mode & 1u) != 0u ? uint2(origin_x, origin_y) : uint2(origin_x, origin_y) * scale);
    if (list_mode != 0u) {
        // the listed dots' output texels, N x N of them a dot one after another (spread2d.hlsl's list, in `pre`)
        const uint t = (group.y * SPREAD_GROUPS_X + group.x) * 64u + lane;
        const uint entry = t / (scale * scale), sub = t % (scale * scale);
        if (entry >= pre[uint2(0u, SPREAD_LIST_ROW)]) {
            return;
        }
        const uint dot = pre[uint2((entry + 1u) % SPREAD_LIST_W, SPREAD_LIST_ROW + (entry + 1u) / SPREAD_LIST_W)];
        id = uint2((dot & 0xffffu) * scale + sub % scale, (dot >> 16) * scale + sub / scale);
    }
    if ((pre_mode & 1u) != 0u) {
        // the pass at the DS's size, and pre_mode 3: one thread a DS pixel, its colour at the first place in it
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
            verdict = pre[uint2((uint)x * 2u, id.y / scale)] & PRE_VERDICT;
            if (verdict == PRE_NOTHING) {
                // kd_split's 0 at every place of the DS pixel: the 3D is not read for it
                out_screen[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
                return;
            }
        }
    }
    g_place_used = false;
    g_3d_used = false;
    g_el_skip = 0u;
    if (pre_mode == 2u && verdict != PRE_SAME && verdict != PRE_SAME_ALPHA3D) {
        // only the elements the DS-sized pass found showing something in this DS pixel are looked at
        g_el_skip = ~pre[uint2((uint)x * 2u + 1u, id.y / scale)] & PRE_ELEMENTS;
    } else if (pre_mode == 1u) {
        // the elements that show nothing in this DS pixel: no place of it depends on them
        const uint count = min(KD_DESC(KD_ELEMENTS), PRE_ELEMENTS_MOST);
        const int ex = KD_DESC(KD_WIDE) != 0u ? x - (int)KD_DESC(KD_WIDE) : x;
        for (uint e = 0u; e < count; ++e) {
            if (element_empty_at(e, ex, row)) {
                g_el_skip |= 1u << e;
            }
        }
    }
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
        const bool only3d = g_stack_3d_only && KD_DESC(KD_WIDE) == 0u && (flags & KDF_DISPLAY_MASK) == 1u &&
                            (flags & KDF_FORCED_BLANK) == 0u && (flags & KDF_BG0_IS_3D) != 0u &&
                            (flags & (1u << KDF_BG_ON_SHIFT)) != 0u;
        const bool alone3d = only3d && !KD_SPLIT_ON(flags);
        // the one-screen picture's source (kd_split) with only the 3D at the DS pixel: the 3D or the backdrop is on
        // top at every place in it, which kd_split gives 0 for (the 2D does not show over the 3D) -- but a
        // translucent 3D over a backdrop that is a blend's second target, which it blends by the 3D's alpha
        const bool nothing = only3d && KD_SPLIT_ON(flags) &&
                             ((KD_DESC(KD_BLEND) >> 8) & (1u << KD_LAYER_BACKDROP)) == 0u;
        // ... the backdrop a second target: nothing too where the 3D is opaque or absent, else the 3D over the
        // backdrop by its alpha, which spread2d.hlsl works out at each texel
        // (a window that leaves the 3D out there puts the backdrop on top: nothing then; only the 3D lies there, so
        // no sprite's window)
        const bool backdrop = only3d && KD_SPLIT_ON(flags) && !nothing;
        const bool shown3d = backdrop && (kd_mask(x, row, 0u) & 1u) != 0u;
        // the split source over the 3D with nothing finer than the DS pixel: its colour holds wherever the 3D's
        // alpha is the one composed with here (kd_split's a3: kd_alpha3d, kd_moved3d)
        const bool by_alpha = g_3d_used && !g_place_used && KD_SPLIT_ON(flags) && KD_DESC(KD_WIDE) == 0u &&
                              source3d == 2u;
        const uint column = ((uint)x + KD_DESC(KD_HOFS3D)) & 511u;
        const uint a3 = column >= 256u ? 31u : ((g_3d_value >> 29) & 1u) != 0u ? 0u : (g_3d_value >> 24) & 31u;
        verdict = !g_place_used && !g_3d_used ? PRE_SAME
                  : alone3d                   ? PRE_3D_ONLY
                  : nothing || (backdrop && !shown3d) ? PRE_NOTHING
                  : backdrop                  ? PRE_NOTHING_BACKDROP
                  : by_alpha                  ? PRE_SAME_ALPHA3D
                                              : PRE_EACH;
        // the verdict, with why a dot is not PRE_SAME above it (SPREAD_WHY_*, counted with KHDAYS_PERF)
        const uint why = (g_place_used ? SPREAD_WHY_PLACE : 0u) | (g_3d_used ? SPREAD_WHY_3D : 0u) |
                         (g_stack_3d_only ? SPREAD_WHY_STACK3D : 0u) | (only3d ? SPREAD_WHY_ONLY3D : 0u) |
                         (KD_SPLIT_ON(flags) ? SPREAD_WHY_SPLIT : 0u) |
                         (((KD_DESC(KD_BLEND) >> 8) & (1u << KD_LAYER_BACKDROP)) != 0u ? SPREAD_WHY_BACKDROP : 0u) |
                         (KD_DESC(KD_WIDE) != 0u ? SPREAD_WHY_WIDE : 0u);
        pre[uint2(id.x * 2u, id.y)] = verdict | (verdict == PRE_SAME ? 0u : why << PRE_WHY_SHIFT) |
                                      (verdict == PRE_SAME_ALPHA3D ? a3 << PRE_A3_SHIFT : 0u);
        // the colour where it is used (PRE_SAME, PRE_SAME_ALPHA3D); else the elements that show something in the
        // DS pixel (none in a PRE_SAME_ALPHA3D one: no place used)
        pre[uint2(id.x * 2u + 1u, id.y)] =
            verdict == PRE_SAME || verdict == PRE_SAME_ALPHA3D ? c6 : ~g_el_skip & PRE_ELEMENTS;
        return;
    }
    if (pre_mode == 3u) {
        // the DS pixel's colour, composed at its first place, at each of its output pixels (gfx_render.c says when)
        const uint flags = KD_DESC(KD_FLAGS);
        const float4 whole = (flags & KDF_SPLIT) != 0u
                                 ? float4((float)(c6 & 255u), (float)((c6 >> 8) & 255u), (float)((c6 >> 16) & 255u),
                                          (float)(c6 >> 24)) / 255.0f
                                 : (flags & KDF_FINE) != 0u
                                       ? float4((float)(c6 & 255u) / 255.0f, (float)((c6 >> 8) & 255u) / 255.0f,
                                                (float)((c6 >> 16) & 255u) / 255.0f, 1.0f)
                                       : float4(channel8(c6 & 63u), channel8((c6 >> 8) & 63u),
                                                channel8((c6 >> 16) & 63u), 1.0f);
        for (uint fy = 0u; fy < scale; ++fy) {
            for (uint fx = 0u; fx < scale; ++fx) {
                out_screen[id.xy * scale + uint2(fx, fy)] = whole;
            }
        }
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
