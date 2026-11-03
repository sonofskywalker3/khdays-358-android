// Each layer's colour at each DS pixel, looked up once for compose2d.hlsl (which composes N x N output
// pixels from it): compose.h's kd_layer at 1x. A pixel whose colour depends on the place inside the DS
// pixel (a replacement's texel) or on the 3D is marked LAYER_LIVE, and compose2d looks it up itself.

#include "../gfx/frame.h"

// The same buffers as compose2d.hlsl, bound alike (gfx_render.c)
ByteAddressBuffer desc : register(t0, space0);
ByteAddressBuffer bg_mem : register(t1, space0);
ByteAddressBuffer obj_mem : register(t2, space0);
ByteAddressBuffer bgext_mem : register(t3, space0);
ByteAddressBuffer objext_mem : register(t4, space0);
ByteAddressBuffer vram_mem : register(t5, space0);
ByteAddressBuffer layer3d_mem : register(t6, space0);
ByteAddressBuffer hd : register(t7, space0);

[[vk::image_format("r32ui")]] RWTexture2D<uint> layers : register(u0, space1); // 256 x (4 x rows): layer b's rows from b x rows

cbuffer Params : register(b0, space2) {
    uint rows;       // the rows looked up, from row0
    uint row0;
    uint layers_off; // compose.h KD_LAYERS_OFF
    uint pad;
};

#define LAYER_LIVE 0xffffffffu
static uint g_hd_seen;
#define KD_HD_SEEN g_hd_seen

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
#define KD_3D(x, row) 0u // the 3D layer is looked up live (LAYER_LIVE)
#define KD_3D_KNOWN 0
// the maps after the memories: engine A's layers' 512 KB and sprites' 256 KB, engine B's 128 KB each
#define KD_HD_BG(u) bg_mem.Load((((KD_DESC(KD_FLAGS) & KDF_ENGINE_B) != 0u ? 0x8000u : 0x20000u) + (u)) * 4u)
#define KD_HD_OBJ(u) obj_mem.Load((((KD_DESC(KD_FLAGS) & KDF_ENGINE_B) != 0u ? 0x8000u : 0x10000u) + (u)) * 4u)
#define KD_HD_FIRST(id) hd.Load((id) * 16u)
#define KD_HD_WIDTH(id) hd.Load(((id) * 4u + 1u) * 4u)
#define KD_HD_HEIGHT(id) hd.Load(((id) * 4u + 2u) * 4u)
#define KD_HD_INFO(id) hd.Load(((id) * 4u + 3u) * 4u)
#define KD_HD_TEXEL(i) hd.Load((i) * 4u)
#define KD_SUB_X 0.5f // a replacement read: the colour is then looked up live
#define KD_SUB_Y 0.5f
#define KD_PIXEL 1.0f
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

#define KD_SPRITE_ROW(row, word) desc.Load((KD_ROWS_AT + ((uint)(row) & 255u) * 4u + (word)) * 4u)
#define KD_LOW_BIT(m) firstbitlow(m)

#include "../gfx/compose.h"

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    const uint flags = KD_DESC(KD_FLAGS) & ~(uint)(KD_LAYERS_OFF);
    const int x = (int)id.x, row = (int)(id.y + row0);
    if (id.x >= 256u || id.y >= rows) {
        return;
    }
    for (uint b = 0u; b < 4u; ++b) {
        uint c = 0u;
        if ((flags & (1u << (KDF_BG_ON_SHIFT + b))) != 0u) {
            const uint kind = KD_DESC(KD_LAYERS + b * KD_LAYER_WORDS + KDL_KIND) & 0xffu;
            if (kind == KDK_3D) {
                c = LAYER_LIVE;
            } else {
                g_hd_seen = 0u;
                c = kd_layer((int)b, x, row);
                if (g_hd_seen != 0u) {
                    c = LAYER_LIVE;
                }
            }
        }
        layers[uint2(id.x, b * rows + id.y)] = c;
    }
}
