/* The port's own 2D, composed on the graphics card (gfx_render.h). Each frame uploads both screens'
 * descriptions and palettes, the 3D at 1x and the 2D memory pages that changed; compose2d.hlsl makes
 * each screen at N x, wrapped as a texture of the window's renderer; on one screen onescreen.hlsl
 * then makes the single picture from both. */
#include "gfx_render.h"
#include "gpu_hd.h"
#include "kh2_sequence_gpu.h"
#include "gpu_raster3d.h"
#include "two_screens.h"
#include "../../runtime/display/frame_package.h"
#include "generated/capture_cs.h"
#include "generated/compose2d_cs.h"
#if defined(__ANDROID__)
#include "generated/compose2d_plain_cs.h"
#include "generated/compose2d_split_cs.h"
#include "generated/compose2d_wide_cs.h"
#endif
#include "generated/layers2d_cs.h"
#include "generated/onescreen_cs.h"
#include "generated/onescreen_plain_cs.h"
#include "generated/shrink_cs.h"
#include "../../gfx/onescreen.h"
#include "../screens/game_pause.h"

#include <stdio.h>
#include <string.h>

#define PAGE KHDAYS_FRAME_VRAM_PAGE

/* compose2d.hlsl's parameters, as this file fills them in; compose_params gives them to the shader packed into
 * its constant buffer's 32 words (the shader says why no more). */
typedef struct Compose2dParams {
    uint32_t scale, width, height, source3d, disp3dcnt, swap_params, clear_color, clear_depth;
    uint32_t fog_color, fog_offset, fine, offset3d;
    uint32_t edge_colors[4];
    uint32_t fog_table[8];
    uint32_t scale3d, width3d, height3d, smooth3d;  /* the 3D's own scale (the 2D finer than it) */
    uint32_t hud3d, hud_width, hud_height; /* the HUD's 3D apart (gpu_raster3d.h), at `scale` */
    uint32_t row0, layers_off;   /* rows past the DS's (lower_layout.h), layers left out */
    uint32_t layers1x_rows; /* layers2d.hlsl's rows from row0, or 0 */
    uint32_t pre_mode; /* compose2d.hlsl: 0 each pixel composed, 1 the DS-sized pass, 2 its verdicts read (pre2d),
                        * 3 a thread a DS pixel (coarse_2d) */
    uint32_t origin_x, origin_y; /* the DS pixel the dispatch starts at, multiples of 8 (one_reads' tiles) */
} Compose2dParams;

/* compose2d.hlsl's constant buffer, register for register. */
typedef struct Compose2dBlock {
    uint32_t scale, width, height, modes, disp3dcnt, swap_params, clear_color, clear_depth;
    uint32_t fog_color, fog_offset, fine, offset3d;
    uint32_t edge_colors[4];
    uint32_t fog_table[8];
    uint32_t scale3d, width3d, height3d, hud_width;
    uint32_t hud_height, row0, layers_off, layers1x_rows;
} Compose2dBlock;
SDL_COMPILE_TIME_ASSERT(compose2d_block_words, sizeof(Compose2dBlock) == 32 * sizeof(uint32_t));

/* The next compose2d pass's parameters. */
static void compose_params(SDL_GPUCommandBuffer *cmd, const Compose2dParams *p)
{
    Compose2dBlock b;
    b.scale = p->scale;
    b.width = p->width;
    b.height = p->height;
    /* compose2d.hlsl `modes`: source3d in bits 0-1, smooth3d in 2, hud3d in 3, pre_mode in 4-5, and the origin in
     * eighths (one_reads' tiles): x in bits 8-19, y in 20-31. In this word, the block's 32 words all taken */
    b.modes = (p->source3d & 3u) | (p->smooth3d != 0u ? 4u : 0u) | (p->hud3d != 0u ? 8u : 0u) | (p->pre_mode & 3u) << 4 |
              ((p->origin_x / 8u) & 0xfffu) << 8 | ((p->origin_y / 8u) & 0xfffu) << 20;
    b.disp3dcnt = p->disp3dcnt;
    b.swap_params = p->swap_params;
    b.clear_color = p->clear_color;
    b.clear_depth = p->clear_depth;
    b.fog_color = p->fog_color;
    b.fog_offset = p->fog_offset;
    b.fine = p->fine;
    b.offset3d = p->offset3d;
    memcpy(b.edge_colors, p->edge_colors, sizeof(b.edge_colors));
    memcpy(b.fog_table, p->fog_table, sizeof(b.fog_table));
    b.scale3d = p->scale3d;
    b.width3d = p->width3d;
    b.height3d = p->height3d;
    b.hud_width = p->hud_width;
    b.hud_height = p->hud_height;
    b.row0 = p->row0;
    b.layers_off = p->layers_off;
    b.layers1x_rows = p->layers1x_rows;
    SDL_PushGPUComputeUniformData(cmd, 0, &b, sizeof(b));
}

/* layers2d.hlsl's constant buffer, register for register. */
typedef struct Layers2dParams {
    uint32_t rows, row0, layers_off, pad;
} Layers2dParams;

/* onescreen.hlsl's constant buffer, register for register. */
typedef struct OneScreenParams {
    uint32_t scale, width, height, has3d, disp3dcnt, swap_params, clear_color, clear_depth;
    uint32_t fog_color, fog_offset, fine, shape_count;
    uint32_t edge_colors[4];
    uint32_t fog_table[8];
    uint32_t frame, rule, masters, backdrop;
    uint32_t a_upper, backdrop_w, backdrop_h, hide3d;
    float backdrop_scale, backdrop_x, backdrop_y; /* picture texels per its texel, its top-left */
    uint32_t piece_count;                         /* an animated picture's pieces this frame */
    uint32_t animated;                            /* 1: an animated picture (`backdrop` its ground) */
    uint32_t bin_tile, bin_tiles_x, bin_tiles_y;  /* its pieces' tiles (bin_pieces), after the pieces */
    uint32_t scale3d, width3d, height3d, smooth3d; /* the 3D's own scale (the picture finer than it) */
    uint32_t hud3d;                                /* the HUD's 3D apart (gpu_raster3d.h), at `scale` */
    uint32_t origin_x, origin_y, unused7;          /* the texel a dispatch starts at (one_covers) */
} OneScreenParams;

/* The areas of the package's 2D memory, each in a buffer of its own. */
enum { ABG, AOBJ, BBG, BOBJ, ABGEXT, AOBJEXT, BBGEXT, BOBJEXT, VRAM, AREAS };
static const struct {
    int first, pages;
} area[AREAS] = {
    {KHDAYS_2D_ABG, 32},    {KHDAYS_2D_AOBJ, 16},  {KHDAYS_2D_BBG, 8},     {KHDAYS_2D_BOBJ, 8}, {KHDAYS_2D_ABGEXT, 2},
    {KHDAYS_2D_AOBJEXT, 1}, {KHDAYS_2D_BBGEXT, 2}, {KHDAYS_2D_BOBJEXT, 1}, {KHDAYS_2D_VRAM, 8},
};

static SDL_GPUDevice *device;
static SDL_GPUComputePipeline *compose;
/* Instead of it where its driver can't compile all of it (make_compose_parts): one for each kind of frame. */
static SDL_GPUComputePipeline *compose_wide, *compose_plain, *compose_split;
static SDL_GPUComputePipeline *compose_engine[2]; /* this frame's, by what the engine's description ended up as */
/* Eight buffers per pass, the most SDL's GPU API binds. The layers' and sprites' memory buffers also
 * hold the map of their replaced (HD) tiles; each description buffer also holds its palettes. */
static SDL_GPUBuffer *memory[AREAS], *desc[2], *layer3d;
static uint32_t hd2d_version[KHDAYS_FRAME_HD2D_PAGES]; /* the change counts of the card's copy */
#define HD2D_PAGE (KHDAYS_FRAME_HD2D_PAGE_WORDS * 4u)
#define DESC_BYTES (KHDAYS_DESC_WORDS * 4u + 0x400u) /* the description, then the palettes */
/* ... then the screen's elements (gfx/elements.h); each buffer grows to the most any frame needed */
static uint32_t desc_bytes[2];

/* KHDAYS_GPU_SKIP=a,b: passes not dispatched, to measure what each costs (the picture is then wrong) */
/* KHDAYS_GPU_TIME: each submission waited for, after the card is idle, and its time said every 120 */
static int gpu_submit_timed(SDL_GPUDevice *dev, SDL_GPUCommandBuffer *cmd, const char *label)
{
    static const char *labels[8];
    static double total_ms[8];
    static int count[8];
    SDL_GPUFence *fence;
    Uint64 t0;
    int k = 0;
    if (SDL_getenv("KHDAYS_GPU_TIME") == NULL) {
        return SDL_SubmitGPUCommandBuffer(cmd);
    }
    while (k < 7 && labels[k] != NULL && SDL_strcmp(labels[k], label) != 0) {
        ++k;
    }
    labels[k] = label;
    SDL_WaitForGPUIdle(dev); /* what came before not counted */
    t0 = SDL_GetTicksNS();
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == NULL) {
        return 0;
    }
    SDL_WaitForGPUFences(dev, true, &fence, 1);
    SDL_ReleaseGPUFence(dev, fence);
    total_ms[k] += (double)(SDL_GetTicksNS() - t0) / 1e6;
    if (++count[k] == 120) {
        fprintf(stderr, "gpu time: %s %.2f ms on average\n", label, total_ms[k] / count[k]);
        total_ms[k] = 0.0;
        count[k] = 0;
    }
    return 1;
}

/* KHDAYS_GPU_TIME: the work so far submitted and timed as `label`, a new command buffer for the rest */
static void gpu_split(SDL_GPUDevice *dev, SDL_GPUCommandBuffer **cmd, const char *label)
{
    if (SDL_getenv("KHDAYS_GPU_TIME") != NULL) {
        gpu_submit_timed(dev, *cmd, label);
        *cmd = SDL_AcquireGPUCommandBuffer(dev);
    }
}

static int gpu_skip(const char *pass)
{
    const char *list = SDL_getenv("KHDAYS_GPU_SKIP");
    return list != NULL && SDL_strstr(list, pass) != NULL;
}

#define SPRITE_ROW_BYTES (256u * 4u * 4u) /* each row's sprites: a bit each, 128 */

static uint32_t element_bytes(const KhdaysFramePackage *p, int e)
{
    /* ... and the parts left out (KD_LEFT_OUT), and each row's sprites (sprite_rows) */
    const uint32_t left = p->desc2d[e][KD_LEFT_OUT] != 0 ? (KE_LEFT_CELL_WORDS + KE_LEFT_OBJ_WORDS) * 4u : 0u;
    return p->elements[e].count * KE_WORDS * 4u + left + SPRITE_ROW_BYTES;
}

/* The sprites each row (modulo 256) shows, for compose.h kd_sprites to look at only those: a sprite in
 * the rows of its box, as kd_sprites measures them. */
static int every_sprite = -1; /* KHDAYS_SPRITE_ROWS=all: every sprite on every row, as before the lists */

void khdays_gfx_render_every_sprite(int every)
{
    every_sprite = every;
}

static void sprite_rows(const uint32_t *description, uint32_t *rows)
{
    if (every_sprite < 0) {
        every_sprite =
            SDL_getenv("KHDAYS_SPRITE_ROWS") != NULL && SDL_strcmp(SDL_getenv("KHDAYS_SPRITE_ROWS"), "all") == 0;
    }
    memset(rows, every_sprite ? 0xff : 0, SPRITE_ROW_BYTES);
    if (every_sprite) {
        return;
    }
    for (uint32_t i = 0; i < 128u; ++i) {
        const uint32_t base = KD_SPRITES + i * KD_SPRITE_WORDS;
        const uint32_t box = description[base + KDS_BOX], y = box & 0xffu, height = (box >> 16) & 0xffu;
        if (!(description[base + KDS_SIZE] & KDS_SHOWN)) {
            continue;
        }
        for (uint32_t dy = 0; dy < height; ++dy) {
            rows[((y + dy) & 255u) * 4u + i / 32u] |= 1u << (i % 32u);
        }
    }
}

/* The size of area `a`'s buffer: its pages and, for the layers' and the
 * sprites' memory, a 2 KB map page for each page. */
static Uint32 area_bytes(int a)
{
    return (Uint32)area[a].pages * (PAGE + (a <= BOBJ ? HD2D_PAGE : 0u));
}
static SDL_GPUTexture *dummy3d[2];   /* bound in place of the 3D targets at 1x */
static SDL_GPUTexture *dummy_hud[2]; /* and of the HUD's apart when there is none */
static SDL_GPUTexture *screen[2];
/* layers2d.hlsl: each engine's layers at each DS pixel, looked up once, before compose2d.hlsl's pass */
static SDL_GPUComputePipeline *layers_pipeline;
static SDL_GPUTexture *layers1x[2];
/* compose2d.hlsl's DS-sized pass: per DS pixel (PRE_COLUMNS of them a row) whether its output pixels can skip the
 * composition, and its colour when they all have it */
static SDL_GPUTexture *pre2d[2];
#define PRE_COLUMNS (4 * KHDAYS_FRAME_W)
#define PRE_SCALE_MIN 3 /* below it the DS-sized pass costs more than it spares (2x on the TCL: 30 fps with, 33 without) */
/* The lower screen's rows past the DS's 192 (or under its frame) for a taller second display, composed
 * with some layers left out (host/screens/lower_layout.h): asked for, then drawn into `rows_texture`. */
static int rows_row0, rows_count, rows_scale, rows_drawn;
static uint32_t rows_off;
static SDL_GPUTexture *rows_texture;
static int screen_scale, screen_columns[2]; /* their scale, and each one's width in DS columns */
static SDL_Texture *wrapped[2];
static SDL_Renderer *wrapped_for[2];
static SDL_GPUTransferBuffer *upload;
static uint32_t upload_size;
/* The one-screen picture: its shader, its shapes and keys (each buffer grows to the most any frame
 * needed), and the picture. */
static SDL_GPUComputePipeline *onescreen;
static SDL_GPUComputePipeline *onescreen_plain; /* its part for the texels no shape reaches (Vulkan), or NULL */
static SDL_GPUBuffer *shape_buffer, *key_buffer, *piece_buffer;
static uint32_t shape_buffer_bytes, key_buffer_bytes, piece_buffer_bytes;
#define PIECE_BYTES 128u /* ui/composition.h KhdaysPiece: KHDAYS_PIECE_WORDS words */
#define BIN_TILE 64u     /* an animated picture's tiles, in its texels (bin_pieces) */

/* An animated picture's pieces sorted into tiles of BIN_TILE x BIN_TILE of its texels, for onescreen.hlsl
 * to test only the pieces over a texel's tile: each texel testing every piece (some 70 of the title's, at
 * some 2.4 million texels) held the Thor's card a second a picture. After the pieces in their buffer:
 * for each tile (rows of bin_tiles_x) the first of its pieces in the list and their count, then the list,
 * the pieces' numbers in their order (the order they are drawn in). */
static uint32_t *bins;
static uint32_t bins_room, bins_words, bin_tiles_x, bin_tiles_y;

/* The tiles piece `r` (its rectangle: x0, y0, x1, y1) lies over; 0 when none of it is in the picture. */
static int piece_tiles(const float *r, int picture_w, int picture_h, int *tx0, int *ty0, int *tx1, int *ty1)
{
    const float x0 = r[0] < r[2] ? r[0] : r[2], x1 = r[0] < r[2] ? r[2] : r[0];
    const float y0 = r[1] < r[3] ? r[1] : r[3], y1 = r[1] < r[3] ? r[3] : r[1];
    if (!(x1 > 0.0f && y1 > 0.0f && x0 < (float)picture_w && y0 < (float)picture_h)) {
        return 0;
    }
    *tx0 = x0 > 0.0f ? (int)(x0 / (float)BIN_TILE) : 0;
    *ty0 = y0 > 0.0f ? (int)(y0 / (float)BIN_TILE) : 0;
    *tx1 = SDL_min((int)((x1 - 0.001f) / (float)BIN_TILE), (int)bin_tiles_x - 1);
    *ty1 = SDL_min((int)((y1 - 0.001f) / (float)BIN_TILE), (int)bin_tiles_y - 1);
    return 1;
}

static int bin_pieces(const void *pieces, int count, int picture_w, int picture_h)
{
    const float *f = (const float *)pieces;
    uint32_t tiles, listed = 0, start = 0;
    int tx0, ty0, tx1, ty1;
    bin_tiles_x = ((uint32_t)SDL_max(picture_w, 1) + BIN_TILE - 1u) / BIN_TILE;
    bin_tiles_y = ((uint32_t)SDL_max(picture_h, 1) + BIN_TILE - 1u) / BIN_TILE;
    tiles = bin_tiles_x * bin_tiles_y;
    for (int i = 0; i < count; ++i) {
        if (piece_tiles(f + (size_t)i * (PIECE_BYTES / 4u), picture_w, picture_h, &tx0, &ty0, &tx1, &ty1)) {
            listed += (uint32_t)((tx1 - tx0 + 1) * (ty1 - ty0 + 1));
        }
    }
    bins_words = 2u * tiles + listed;
    if (bins_words > bins_room) {
        uint32_t *grown = (uint32_t *)SDL_realloc(bins, bins_words * 4u);
        if (grown == NULL) {
            bins_words = 0;
            return 0;
        }
        bins = grown;
        bins_room = bins_words;
    }
    /* each tile's count, then where its list starts, then the lists in the pieces' order */
    SDL_memset(bins, 0, 2u * tiles * 4u);
    for (int i = 0; i < count; ++i) {
        if (piece_tiles(f + (size_t)i * (PIECE_BYTES / 4u), picture_w, picture_h, &tx0, &ty0, &tx1, &ty1)) {
            for (int ty = ty0; ty <= ty1; ++ty) {
                for (int tx = tx0; tx <= tx1; ++tx) {
                    ++bins[2u * ((uint32_t)ty * bin_tiles_x + (uint32_t)tx) + 1u];
                }
            }
        }
    }
    for (uint32_t t = 0; t < tiles; ++t) {
        bins[2u * t] = start;
        start += bins[2u * t + 1u];
        bins[2u * t + 1u] = 0;
    }
    for (int i = 0; i < count; ++i) {
        if (piece_tiles(f + (size_t)i * (PIECE_BYTES / 4u), picture_w, picture_h, &tx0, &ty0, &tx1, &ty1)) {
            for (int ty = ty0; ty <= ty1; ++ty) {
                for (int tx = tx0; tx <= tx1; ++tx) {
                    const uint32_t t = (uint32_t)ty * bin_tiles_x + (uint32_t)tx;
                    bins[2u * tiles + bins[2u * t] + bins[2u * t + 1u]++] = (uint32_t)i;
                }
            }
        }
    }
    return 1;
}
static SDL_GPUTexture *picture;
static int picture_w, picture_h, picture_columns;
static SDL_Texture *picture_wrapped;
static SDL_Renderer *picture_for;
/* The composition's picture behind everything (`backdrop`), uploaded once, and a texel bound in
 * its place when there is none. */
static SDL_GPUTexture *no_backdrop;
/* The backdrops on the card: a few kept, so going back and forth between two (the title's menu and its new
 * game's dialogs: atlases of 13-17 MB) uploads neither again. Each known by its pixels' address, its size
 * and a sample of its texels (an address can be used again by another picture). */
#define BACKDROPS_KEPT 3
static struct {
    SDL_GPUTexture *texture;
    const uint8_t *of;
    int w, h;
    uint32_t sample;
    uint32_t used; /* backdrop_clock when last used */
} backdrops[BACKDROPS_KEPT];
static uint32_t backdrop_clock;
/* The 3D on both screens (Gfx_SetupSubScreenCapture): each game frame the DS captures engine A's
 * picture, and the next one engine B shows it on the other screen. The port's capture is at the DS's
 * size (display.c), so for that screen this keeps engine A's picture of the previous package when the
 * capture was in use, one for each screen it was on, and remembers the last package drawn and the one
 * before it. Which copy shows: two_screens.h. */
typedef struct CaptureCopy {
    SDL_GPUTexture *texture;
    int w, h, columns, ok;
    uint32_t frame; /* the package it is the picture of */
    SDL_Texture *wrapped;
    SDL_Renderer *wrapped_for;
} CaptureCopy;
static CaptureCopy capture_copy[2]; /* engine A's picture when it was on the upper screen, on the lower */
static void drop_capture_copy(CaptureCopy *k);
static uint32_t drawn_frame, drawn_before;
static int drawn_any, drawn_capture, drawn_upper, drawn_both;
/* The screen blends (a cut between an event's camera shots, Ov023_CmdStartScreenBlend): engine A shows a
 * VRAM bank while its picture, blended with that bank, is captured back into it. The port's capture is at
 * the DS's size, so each one is taken again here from what the window shows, and the bank shown from
 * that copy while the game's bank still holds the same capture (display.c package_capture). */
typedef struct HdBank {
    SDL_GPUTexture *texture;
    int w, h;
    uint32_t captures; /* the game's count of captures into the bank when this copy was taken */
    int ok;
} HdBank;
static SDL_GPUComputePipeline *capturer;
static HdBank hd_bank[4], hd_scratch, hd_source;
/* What the window shows: the screens and the picture made larger than shown (the internal resolution
 * rounded up, or above the window's) are shrunk on the card (shrink.hlsl) to `shown` window pixels a DS
 * pixel, into these: the window shows the picture at its own pixels rather than a smaller one
 * stretched. */
typedef struct Shrunk {
    SDL_GPUTexture *texture;
    int w, h;
    SDL_Texture *wrapped;
    SDL_Renderer *wrapped_for;
} Shrunk;
static SDL_GPUComputePipeline *shrinker;
static float shown; /* the window's pixels a DS pixel; 0: as made */
static Shrunk shrunk_screen[2], shrunk_picture;
static int screens_shrunk, picture_shrunk;      /* the last draw's were */
static uint32_t version[KHDAYS_FRAME_2D_PAGES]; /* the change counts of the card's copy */
static int valid;
/* The Moogle Shop's screens composed apart (khdays_gfx_render_shop): by screen (0 the upper) and variant, the
 * description (its words changed, then the engine's palettes and elements, as desc[e]), the texture and its
 * wrapping, and the count of shop pictures when it was last composed (0: not now). */
static SDL_GPUBuffer *shop_desc[2][KHDAYS_SHOP_VARIANTS];
static uint32_t shop_desc_bytes[2][KHDAYS_SHOP_VARIANTS];
static SDL_GPUTexture *shop_screen[2][KHDAYS_SHOP_VARIANTS];
static SDL_Texture *shop_wrapped[2][KHDAYS_SHOP_VARIANTS];
static SDL_Renderer *shop_wrapped_for[2][KHDAYS_SHOP_VARIANTS];
static unsigned shop_made[2][KHDAYS_SHOP_VARIANTS];
static unsigned shop_pictures;
static int shop_scale;

static SDL_GPUBuffer *make_buffer(Uint32 size)
{
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    info.size = size;
    return SDL_CreateGPUBuffer(device, &info);
}

static SDL_GPUTexture *make_texture(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int w, int h)
{
    SDL_GPUTextureCreateInfo info;
    SDL_zero(info);
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = usage;
    info.width = (Uint32)w;
    info.height = (Uint32)h;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(device, &info);
}

static SDL_GPUComputePipeline *make_pipeline_of(const unsigned char *dxil, unsigned dxil_size,
                                                const unsigned char *spirv, unsigned spirv_size,
                                                const unsigned char *msl, unsigned msl_size, const char *entry,
                                                int buffers, int textures)
{
    SDL_GPUComputePipelineCreateInfo info;
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    SDL_zero(info);
    if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        info.code = dxil;
        info.code_size = dxil_size;
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
    } else if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.code = spirv;
        info.code_size = spirv_size;
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        info.code = msl;
        info.code_size = msl_size - 1; /* the text without its final zero */
        info.format = SDL_GPU_SHADERFORMAT_MSL;
    } else {
        SDL_SetError("the device takes none of the shader formats this build carries");
        return NULL;
    }
    info.entrypoint = entry;
    info.num_readonly_storage_buffers = (Uint32)buffers;
    info.num_readwrite_storage_textures = (Uint32)textures;
    info.num_uniform_buffers = 1;
    info.threadcount_x = 8;
    info.threadcount_y = 8;
    info.threadcount_z = 1;
    return SDL_CreateGPUComputePipeline(device, &info);
}

static SDL_GPUComputePipeline *make_layers_pipeline(void)
{
    return make_pipeline_of(layers2d_cs_dxil, layers2d_cs_dxil_size, layers2d_cs_spirv, layers2d_cs_spirv_size,
                            layers2d_cs_msl, layers2d_cs_msl_size, LAYERS2D_CS_ENTRY, 8, 1);
}

/* What draws a frame with this description. */
static SDL_GPUComputePipeline *compose_for(uint32_t wide, uint32_t flags)
{
    if (compose != NULL) {
        return compose;
    }
    return wide != 0 ? compose_wide : (flags & KDF_SPLIT) != 0 ? compose_split : compose_plain;
}

#if defined(__ANDROID__)
/* The whole shader takes Mali's compiler over 3 GB of the process's memory (a Mali-G57, measured 2026-11-03: it
 * fails, or the process is aborted with it, so it can't be tried first); the three parts peak at 220-580 MB
 * and 2-3 s each. */
/* An Adreno older than the 700s gets the parts as well: a Galaxy S10+'s 640 (driver 512.502, little memory
 * free) refused the whole shader, then a part after it; each part is a third of the compiler's work. */
#define PARTS_BELOW_ADRENO 700

static int compose_too_big_here(void)
{
    const char *name = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device), SDL_PROP_GPU_DEVICE_NAME_STRING, "");
    const char *adreno = SDL_strstr(name, "Adreno");
    int model = 0;
    while (adreno != NULL && *adreno != '\0' && (*adreno < '0' || *adreno > '9')) {
        ++adreno;
    }
    if (adreno != NULL) {
        model = SDL_atoi(adreno);
    }
    return SDL_strstr(name, "Mali") != NULL || (model > 0 && model < PARTS_BELOW_ADRENO);
}

/* One part of the shader, its result in the log (a player's log says which one the card refused). */
static SDL_GPUComputePipeline *make_compose_part(const char *which, const unsigned char *spirv, unsigned int size,
                                                 const char *entry)
{
    SDL_GPUComputePipeline *made = make_pipeline_of(NULL, 0, spirv, size, NULL, 1, entry, 8, 7);
    if (made == NULL) {
        fprintf(stderr, "host: the 2D's shader, part %s: refused (%s)\n", which, SDL_GetError());
    } else {
        fprintf(stderr, "host: the 2D's shader, part %s: made\n", which);
    }
    return made;
}

static void make_compose_parts(void)
{
    compose_wide = make_compose_part("wide", compose2d_wide_cs_spirv, compose2d_wide_cs_spirv_size,
                                     COMPOSE2D_WIDE_CS_ENTRY);
    compose_plain = make_compose_part("plain", compose2d_plain_cs_spirv, compose2d_plain_cs_spirv_size,
                                      COMPOSE2D_PLAIN_CS_ENTRY);
    compose_split = make_compose_part("split", compose2d_split_cs_spirv, compose2d_split_cs_spirv_size,
                                      COMPOSE2D_SPLIT_CS_ENTRY);
}
#endif

static SDL_GPUComputePipeline *make_pipeline(void)
{
    return make_pipeline_of(compose2d_cs_dxil, compose2d_cs_dxil_size, compose2d_cs_spirv, compose2d_cs_spirv_size,
                            compose2d_cs_msl, compose2d_cs_msl_size, COMPOSE2D_CS_ENTRY, 8, 7);
}

static void drop_shrunk(Shrunk *s)
{
    if (s->wrapped != NULL)
        SDL_DestroyTexture(s->wrapped);
    if (s->texture != NULL)
        SDL_ReleaseGPUTexture(device, s->texture);
    SDL_zerop(s);
}

static void drop_hd_bank(HdBank *b)
{
    if (b->texture != NULL)
        SDL_ReleaseGPUTexture(device, b->texture);
    SDL_zerop(b);
}

/* Makes sure `b` has a texture of w x h (its contents are not kept when it is made again). */
static int ensure_hd_bank(HdBank *b, int w, int h)
{
    if (b->texture != NULL && b->w == w && b->h == h) {
        return 1;
    }
    drop_hd_bank(b);
    b->texture = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                              SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, w, h);
    b->w = b->texture != NULL ? w : 0;
    b->h = b->texture != NULL ? h : 0;
    return b->texture != NULL;
}

static void copy_texture(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *from, SDL_GPUTexture *to, int w, int h)
{
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureLocation a, b;
    SDL_zero(a);
    SDL_zero(b);
    a.texture = from;
    b.texture = to;
    SDL_CopyGPUTextureToTexture(copy, &a, &b, (Uint32)w, (Uint32)h, 1, false);
    SDL_EndGPUCopyPass(copy);
}

/* What a package asks of the screen blends: the bank engine A shows from its copy (-1 none), that copy
 * made now from this frame (`seed`), the capture taken again into bank `capture` (-1 none), reading bank
 * `source_b` (-1 none), with EVA and EVB. */
typedef struct CapturePlan {
    int show, seed, capture, source_b;
    uint32_t eva, evb;
    int paused; /* the Menu Overhaul's pause is up over the bank shown: engine A's layers instead */
} CapturePlan;

static void plan_capture(const KhdaysFramePackage *p, CapturePlan *plan)
{
    const uint32_t flags = p->desc2d[0][KD_FLAGS], cnt = p->dispcapcnt, display = flags & KDF_DISPLAY_MASK;
    const int bank = (int)(p->desc2d[0][KD_VRAM] & 3u); /* engine A's VRAM block: shown, and source B */
    const HdBank *copy = &hd_bank[bank];
    const int intact = (p->bank_intact >> bank) & 1u, plain = p->bank_plain[bank] != 0u;
    int current, seed;
    plan->show = plan->capture = plan->source_b = -1;
    plan->seed = 0;
    plan->eva = plan->evb = 0;
    plan->paused = 0;
    /* engine A shows its layers or a bank (the VRAM display mode), its 3D in BG0 for the capture to
     * take; a blend's first capture comes while it still shows its layers */
    if (capturer == NULL || (display != 1u && display != 2u) || (flags & KDF_FORCED_BLANK) ||
        !(flags & KDF_BG0_IS_3D) || !(flags & (1u << KDF_BG_ON_SHIFT))) {
        return;
    }
    /* a scene paused in its camera's blend with the window's KH2 pause up: the bank holds the game's own
     * pause (captured with it), so engine A's layers are shown instead, its pause left out of them; the
     * scene is still, the blend held */
    if (display == 2u && khdays_game_pause_kh2_showing()) {
        plan->paused = 1;
        return;
    }
    /* the copy is of this blend: taken at its first capture or after, though the window may have missed
     * some since (a package it never drew); else, when the bank holds that first capture alone (engine A's
     * picture a VBlank before), it is made again from this frame */
    current = intact && plain && copy->ok && copy->captures >= p->bank_plain[bank] &&
              copy->captures <= p->bank_captures[bank];
    seed = !current && intact && plain && p->bank_plain[bank] == p->bank_captures[bank];
    if (display == 2u && (current || seed)) {
        plan->show = bank;
        plan->seed = seed;
    }
    /* a whole screen (256 x 192) at offset 0, of engine A's layers: not its 3D alone, nor the main
     * memory display's FIFO */
    if ((cnt & 0x80000000u) && ((cnt >> 20) & 3u) == 3u && ((cnt >> 18) & 3u) == 0u && ((cnt >> 26) & 3u) == 0u &&
        !(cnt & (3u << 24))) {
        const uint32_t mode = (cnt >> 29) & 3u; /* 0 source A, 1 source B, 2-3 both */
        const uint32_t eva = (cnt & 31u) > 16u ? 16u : cnt & 31u,
                       evb = ((cnt >> 8) & 31u) > 16u ? 16u : (cnt >> 8) & 31u;
        plan->eva = mode == 0 ? 16u : mode == 1 ? 0u : eva;
        plan->evb = mode == 0 ? 0u : mode == 1 ? 16u : evb;
        if (plan->evb == 0u || plan->show == bank || current) {
            plan->capture = (int)((cnt >> 16) & 3u);
            plan->source_b = plan->evb != 0u ? bank : -1;
        }
    }
}

/* Whether engine A is composed as its layers and 3D though it shows a bank: the capture takes them,
 * or the bank's copy covers them. */
static int plan_layers(const KhdaysFramePackage *p, const CapturePlan *plan)
{
    return (p->desc2d[0][KD_FLAGS] & KDF_DISPLAY_MASK) == 2u && (plan->show >= 0 || plan->capture >= 0 || plan->paused);
}

int khdays_gfx_render_capture_frame(const KhdaysFramePackage *p)
{
    CapturePlan plan;
    plan_capture(p, &plan);
    return plan_layers(p, &plan);
}

/* KHDAYS_TRACE_CAPTURE: what each screen blend's frame shows and takes. */
static int trace_capture(void)
{
    static int on = -1;
    if (on < 0) {
        on = SDL_getenv("KHDAYS_TRACE_CAPTURE") != NULL;
    }
    return on;
}

/* After engine A was composed as its layers into `target` (w x h): the bank it shows over it from the
 * copy, and the capture taken again from what was composed. */
static void take_capture(SDL_GPUCommandBuffer *cmd, const KhdaysFramePackage *p, const CapturePlan *plan,
                         SDL_GPUTexture *target, int w, int h)
{
    int show = plan->show;
    SDL_GPUTexture *a = target;
    HdBank *dest = plan->capture >= 0 ? &hd_bank[plan->capture] : NULL;
    if (show >= 0 && (plan->seed || hd_bank[show].w != w || hd_bank[show].h != h)) {
        /* the copy made from this frame (what was composed already shows it): the bank holds engine A's
         * picture of a VBlank before, or its copy is of another size */
        HdBank *made = &hd_bank[show];
        made->ok = ensure_hd_bank(made, w, h);
        if (made->ok) {
            copy_texture(cmd, target, made->texture, w, h);
            made->captures = p->bank_captures[show];
        }
        show = -1;
    }
    if (dest != NULL && plan->source_b >= 0 &&
        (!hd_bank[plan->source_b].ok || hd_bank[plan->source_b].w != w || hd_bank[plan->source_b].h != h)) {
        dest->ok = 0; /* source B has no copy of this size */
        dest = NULL;
    }
    if (dest != NULL && show >= 0) {
        /* source A, before the bank shown covers it */
        if (ensure_hd_bank(&hd_source, w, h)) {
            copy_texture(cmd, target, hd_source.texture, w, h);
            a = hd_source.texture;
        } else {
            dest->ok = 0;
            dest = NULL;
        }
    }
    if (show >= 0) {
        copy_texture(cmd, hd_bank[show].texture, target, w, h);
    }
    if (trace_capture()) {
        fprintf(stderr, "capture: frame %u, %dx%d, bank %d %s, takes into %d (EVA %u, EVB %u, source B %d)\n", p->frame,
                w, h, plan->show,
                show >= 0         ? "shown from its copy"
                : plan->show >= 0 ? "shown, its copy made from this frame"
                                  : "not shown",
                dest != NULL ? plan->capture : -1, plan->eva, plan->evb, plan->source_b);
    }
    if (dest != NULL && ensure_hd_bank(&hd_scratch, w, h)) {
        struct {
            Uint32 width, height, eva, evb;
        } params = {(Uint32)w, (Uint32)h, plan->eva, plan->evb};
        SDL_GPUStorageTextureReadWriteBinding io[3];
        SDL_GPUComputePass *pass;
        HdBank done;
        SDL_zeroa(io);
        io[0].texture = a;                                                                   /* read only */
        io[1].texture = plan->source_b >= 0 ? hd_bank[plan->source_b].texture : no_backdrop; /* read only */
        io[2].texture = hd_scratch.texture;
        pass = SDL_BeginGPUComputePass(cmd, io, 3, NULL, 0);
        SDL_BindGPUComputePipeline(pass, capturer);
        SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
        SDL_DispatchGPUCompute(pass, (Uint32)(w + 7) / 8, (Uint32)(h + 7) / 8, 1);
        SDL_EndGPUComputePass(pass);
        /* the blend becomes the bank's copy; its old texture the next scratch */
        done = hd_scratch;
        hd_scratch = *dest;
        *dest = done;
        dest->captures = p->bank_captures[plan->capture] + 1u;
        dest->ok = 1;
    } else if (dest != NULL) {
        dest->ok = 0;
    }
}

/* `big` (w x h, `columns` DS columns wide) shrunk to the size shown into `s`, made again when its
 * size changes; in `cmd`. 0 when it is not shown smaller than made, or the texture could not be
 * made. */
static int shrink(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *big, int w, int h, int columns, Shrunk *s)
{
    const int sw = SDL_min((int)SDL_lroundf((float)columns * shown), w);
    const int sh = SDL_min((int)SDL_lroundf((float)KHDAYS_FRAME_H * shown), h);
    struct {
        Uint32 width, height, big_width, big_height;
    } params = {(Uint32)sw, (Uint32)sh, (Uint32)w, (Uint32)h};
    if (sw < 1 || sh < 1 || (sw == w && sh == h)) {
        return 0;
    }
    SDL_GPUStorageTextureReadWriteBinding io[2];
    SDL_GPUComputePass *pass;
    if (s->texture == NULL || s->w != sw || s->h != sh) {
        drop_shrunk(s);
        s->texture = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                  SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, sw, sh);
        if (s->texture == NULL) {
            return 0;
        }
        s->w = sw;
        s->h = sh;
    }
    SDL_zeroa(io);
    io[0].texture = big; /* read only */
    io[1].texture = s->texture;
    io[1].cycle = true;
    pass = SDL_BeginGPUComputePass(cmd, io, 2, NULL, 0);
    SDL_BindGPUComputePipeline(pass, shrinker);
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    if (!gpu_skip("shrink"))
        SDL_DispatchGPUCompute(pass, (Uint32)(sw + 7) / 8, (Uint32)(sh + 7) / 8, 1);
    SDL_EndGPUComputePass(pass);
    return 1;
}

/* A card texture (w x h) as a texture of `renderer`, made once for it (in `made`, `made_for`). */
static SDL_Texture *wrap(SDL_Renderer *renderer, SDL_GPUTexture *texture, int w, int h, SDL_Texture **made,
                         SDL_Renderer **made_for)
{
    if (*made == NULL || *made_for != renderer) {
        const SDL_PropertiesID props = SDL_CreateProperties();
        if (*made != NULL) {
            SDL_DestroyTexture(*made);
        }
        SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, texture);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_ABGR8888);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
        *made = SDL_CreateTextureWithProperties(renderer, props);
        SDL_DestroyProperties(props);
        if (*made == NULL) {
            return NULL;
        }
        SDL_SetTextureBlendMode(*made, SDL_BLENDMODE_NONE);
        *made_for = renderer;
    }
    return *made;
}

void khdays_gfx_render_set_shown(float pixels_per_ds_pixel)
{
    shown = pixels_per_ds_pixel > 0.0f ? pixels_per_ds_pixel : 0.0f;
}

int khdays_gfx_render_open(SDL_GPUDevice *d)
{
    int ok;
    device = d;
#if defined(__ANDROID__)
    if (compose_too_big_here()) {
        make_compose_parts();
    } else {
        compose = make_pipeline();
        if (compose == NULL) {
            /* another card's compiler refused it as well: the parts, as on a Mali */
            fprintf(stderr, "host: the 2D's shader cannot be made whole here (%s): in three parts\n",
                    SDL_GetError());
            make_compose_parts();
        }
    }
#else
    compose = make_pipeline();
#endif
    shrinker = make_pipeline_of(shrink_cs_dxil, shrink_cs_dxil_size, shrink_cs_spirv, shrink_cs_spirv_size,
                                shrink_cs_msl, shrink_cs_msl_size, SHRINK_CS_ENTRY, 0, 2);
    onescreen = make_pipeline_of(onescreen_cs_dxil, onescreen_cs_dxil_size, onescreen_cs_spirv, onescreen_cs_spirv_size,
                                 onescreen_cs_msl, onescreen_cs_msl_size, ONESCREEN_CS_ENTRY, 4, 8);
    onescreen_plain = (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV) != 0
                          ? make_pipeline_of(NULL, 0, onescreen_plain_cs_spirv, onescreen_plain_cs_spirv_size, NULL, 1,
                                             ONESCREEN_PLAIN_CS_ENTRY, 4, 8)
                          : NULL; /* (without it the whole shader draws those texels, with no shapes) */
    /* without it the screen blends show the DS's bank, as before */
    capturer = make_pipeline_of(capture_cs_dxil, capture_cs_dxil_size, capture_cs_spirv, capture_cs_spirv_size,
                                capture_cs_msl, capture_cs_msl_size, CAPTURE_CS_ENTRY, 0, 3);
    shape_buffer_bytes = KO_WORDS * 4u * 64u;
    key_buffer_bytes = 256u;
    piece_buffer_bytes = PIECE_BYTES * 64u;
    shape_buffer = make_buffer(shape_buffer_bytes);
    key_buffer = make_buffer(key_buffer_bytes);
    piece_buffer = make_buffer(piece_buffer_bytes);
    no_backdrop = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                               SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, 1, 1);
    ok = (compose != NULL || (compose_wide != NULL && compose_plain != NULL && compose_split != NULL)) &&
         onescreen != NULL && shape_buffer != NULL && key_buffer != NULL && piece_buffer != NULL &&
         no_backdrop != NULL;
    for (int a = 0; a < AREAS; ++a) {
        memory[a] = make_buffer(area_bytes(a));
        ok = ok && memory[a] != NULL;
    }
    /* the DS-sized pass's verdicts are read back through a read-write binding: SDL asks for simultaneous read and
     * write for that, where the card has it */
    const SDL_GPUTextureUsageFlags pre_usage =
        SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTURETYPE_2D,
                                     SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE)
            ? SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE
            : SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    for (int e = 0; e < 2; ++e) {
        desc[e] = make_buffer(DESC_BYTES + SPRITE_ROW_BYTES);
        desc_bytes[e] = DESC_BYTES + SPRITE_ROW_BYTES;

        dummy3d[e] = make_texture(SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, 1, 1);
        layers1x[e] = make_texture(SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                                   KHDAYS_FRAME_W, 4 * KHDAYS_FRAME_H);
        dummy_hud[e] = make_texture(SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, 1, 1);
        pre2d[e] = make_texture(SDL_GPU_TEXTUREFORMAT_R32_UINT, pre_usage, 2 * PRE_COLUMNS, 4 * KHDAYS_FRAME_H);
        ok = ok && desc[e] != NULL && dummy3d[e] != NULL && dummy_hud[e] != NULL && layers1x[e] != NULL &&
             pre2d[e] != NULL;
    }
    layers_pipeline = make_layers_pipeline();
    ok = ok && layers_pipeline != NULL;
    layer3d = make_buffer(KHDAYS_FRAME_PIXELS * 4);
    ok = ok && layer3d != NULL && khdays_gpu_hd_open(d);
    ok = ok && khdays_sequence_gpu_open(d);
    valid = 0;
    if (!ok) {
        char why[256];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        /* which of its passes the card refused, for a player's log */
        fprintf(stderr, "host: the port's 2D: compose %s, onescreen %s, layers %s\n",
                compose != NULL                                                        ? "made"
                : compose_wide != NULL && compose_plain != NULL && compose_split != NULL ? "made in parts"
                                                                                        : "refused",
                onescreen != NULL ? "made" : "refused", layers_pipeline != NULL ? "made" : "refused");
        khdays_gfx_render_close();
        SDL_SetError("%s", why);
        return 0;
    }
    return 1;
}

static void forget_kept(void); /* (kept_screen, below) */

static void drop_screens(void)
{
    forget_kept(); /* the screens kept were these textures' */
    for (int e = 0; e < 2; ++e) {
        if (wrapped[e] != NULL) {
            SDL_DestroyTexture(wrapped[e]);
            wrapped[e] = NULL;
        }
        wrapped_for[e] = NULL;
        if (screen[e] != NULL) {
            SDL_ReleaseGPUTexture(device, screen[e]);
            screen[e] = NULL;
        }
    }
    screen_scale = 0;
    screen_columns[0] = screen_columns[1] = 0;
    if (rows_texture != NULL) {
        SDL_ReleaseGPUTexture(device, rows_texture);
        rows_texture = NULL;
    }
    rows_scale = 0;
    rows_drawn = 0;
}

static void drop_shop_screens(void)
{
    for (int s = 0; s < 2; ++s) {
        for (int v = 0; v < KHDAYS_SHOP_VARIANTS; ++v) {
            if (shop_wrapped[s][v] != NULL) {
                SDL_DestroyTexture(shop_wrapped[s][v]);
            }
            shop_wrapped[s][v] = NULL;
            shop_wrapped_for[s][v] = NULL;
            if (shop_screen[s][v] != NULL) {
                SDL_ReleaseGPUTexture(device, shop_screen[s][v]);
            }
            shop_screen[s][v] = NULL;
            shop_made[s][v] = 0;
        }
    }
    shop_scale = 0;
}

static void drop_shop(void)
{
    drop_shop_screens();
    for (int s = 0; s < 2; ++s) {
        for (int v = 0; v < KHDAYS_SHOP_VARIANTS; ++v) {
            if (shop_desc[s][v] != NULL) {
                SDL_ReleaseGPUBuffer(device, shop_desc[s][v]);
            }
            shop_desc[s][v] = NULL;
            shop_desc_bytes[s][v] = 0;
        }
    }
}

void khdays_gfx_render_close(void)
{
    khdays_sequence_gpu_close();
    if (device == NULL) {
        return;
    }
    drop_screens();
    drop_shop();
    drop_shrunk(&shrunk_screen[0]);
    drop_shrunk(&shrunk_screen[1]);
    drop_shrunk(&shrunk_picture);
    screens_shrunk = picture_shrunk = 0;
    if (shrinker != NULL)
        SDL_ReleaseGPUComputePipeline(device, shrinker);
    shrinker = NULL;
    drop_capture_copy(&capture_copy[0]);
    drop_capture_copy(&capture_copy[1]);
    drawn_any = 0;
    for (int b = 0; b < 4; ++b) {
        drop_hd_bank(&hd_bank[b]);
    }
    drop_hd_bank(&hd_scratch);
    drop_hd_bank(&hd_source);
    if (capturer != NULL)
        SDL_ReleaseGPUComputePipeline(device, capturer);
    capturer = NULL;
    if (picture_wrapped != NULL)
        SDL_DestroyTexture(picture_wrapped);
    if (picture != NULL)
        SDL_ReleaseGPUTexture(device, picture);
    picture_wrapped = NULL;
    picture_for = NULL;
    picture = NULL;
    picture_w = picture_h = picture_columns = 0;
    if (onescreen != NULL)
        SDL_ReleaseGPUComputePipeline(device, onescreen);
    if (onescreen_plain != NULL) {
        SDL_ReleaseGPUComputePipeline(device, onescreen_plain);
        onescreen_plain = NULL;
    }
    for (int i = 0; i < BACKDROPS_KEPT; ++i) {
        if (backdrops[i].texture != NULL)
            SDL_ReleaseGPUTexture(device, backdrops[i].texture);
    }
    SDL_zeroa(backdrops);
    if (no_backdrop != NULL)
        SDL_ReleaseGPUTexture(device, no_backdrop);
    no_backdrop = NULL;
    if (shape_buffer != NULL)
        SDL_ReleaseGPUBuffer(device, shape_buffer);
    if (key_buffer != NULL)
        SDL_ReleaseGPUBuffer(device, key_buffer);
    if (piece_buffer != NULL)
        SDL_ReleaseGPUBuffer(device, piece_buffer);
    piece_buffer = NULL;
    onescreen = NULL;
    shape_buffer = key_buffer = NULL;
    if (compose != NULL)
        SDL_ReleaseGPUComputePipeline(device, compose);
    compose = NULL;
    if (compose_wide != NULL)
        SDL_ReleaseGPUComputePipeline(device, compose_wide);
    if (compose_plain != NULL)
        SDL_ReleaseGPUComputePipeline(device, compose_plain);
    if (compose_split != NULL)
        SDL_ReleaseGPUComputePipeline(device, compose_split);
    compose_wide = compose_plain = compose_split = NULL;
    for (int a = 0; a < AREAS; ++a) {
        if (memory[a] != NULL)
            SDL_ReleaseGPUBuffer(device, memory[a]);
        memory[a] = NULL;
    }
    for (int e = 0; e < 2; ++e) {
        if (desc[e] != NULL)
            SDL_ReleaseGPUBuffer(device, desc[e]);
        if (dummy3d[e] != NULL)
            SDL_ReleaseGPUTexture(device, dummy3d[e]);
        if (layers1x[e] != NULL)
            SDL_ReleaseGPUTexture(device, layers1x[e]);
        if (pre2d[e] != NULL)
            SDL_ReleaseGPUTexture(device, pre2d[e]);
        pre2d[e] = NULL;
        if (dummy_hud[e] != NULL)
            SDL_ReleaseGPUTexture(device, dummy_hud[e]);
        desc[e] = NULL;
        dummy3d[e] = NULL;
        dummy_hud[e] = NULL;
        layers1x[e] = NULL;
    }
    if (layers_pipeline != NULL)
        SDL_ReleaseGPUComputePipeline(device, layers_pipeline);
    layers_pipeline = NULL;
    if (layer3d != NULL)
        SDL_ReleaseGPUBuffer(device, layer3d);
    if (upload != NULL)
        SDL_ReleaseGPUTransferBuffer(device, upload);
    compose = NULL;
    layer3d = NULL;
    upload = NULL;
    upload_size = 0;
    device = NULL;
}

static int screens(int scale, int columns_a)
{
    if (scale != screen_scale || columns_a != screen_columns[0]) {
        drop_screens();
        for (int e = 0; e < 2; ++e) {
            const int columns = e == 0 ? columns_a : KHDAYS_FRAME_W;
            screen[e] = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                     SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                                     columns * scale, KHDAYS_FRAME_H * scale);
            screen_columns[e] = columns;
            if (screen[e] == NULL) {
                char why[256];
                SDL_strlcpy(why, SDL_GetError(), sizeof(why));
                drop_screens();
                SDL_SetError("%s", why);
                return 0;
            }
        }
        screen_scale = scale;
    }
    return 1;
}

static uint32_t io32(const uint8_t *io3d, unsigned offset)
{
    return (uint32_t)io3d[offset] | (uint32_t)io3d[offset + 1] << 8 | (uint32_t)io3d[offset + 2] << 16 |
           (uint32_t)io3d[offset + 3] << 24;
}

/* Which area a page is in, and its offset there. */
static int area_of(int page, uint32_t *offset)
{
    for (int a = 0; a < AREAS; ++a) {
        if (page >= area[a].first && page < area[a].first + area[a].pages) {
            *offset = (uint32_t)(page - area[a].first) * PAGE;
            return a;
        }
    }
    return -1;
}

/* Makes sure the composition's backdrop is on the card (uploaded the first time it is seen);
 * returns its texture, or the stand-in texel when there is none or it cannot be (said). */
static SDL_GPUTexture *backdrop_on_card(SDL_GPUCommandBuffer *cmd, const KhdaysOneScreenDraw *one)
{
    SDL_GPUTransferBufferCreateInfo info;
    SDL_GPUTransferBuffer *transfer;
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion dst;
    SDL_GPUCopyPass *copy;
    void *mapped;
    const Uint32 bytes = (Uint32)one->backdrop_w * (Uint32)one->backdrop_h * 4u;
    uint32_t sample = 2166136261u;
    int slot = 0;
    if (one->backdrop == NULL || one->backdrop_w <= 0 || one->backdrop_h <= 0) {
        return no_backdrop;
    }
    for (Uint32 k = 0; k < 64; ++k) {
        /* 64 texels spread over it (FNV-1a) */
        const Uint32 at = (Uint32)(((uint64_t)bytes / 4u - 1u) * k / 63u) * 4u;
        uint32_t texel;
        SDL_memcpy(&texel, one->backdrop + at, 4);
        sample = (sample ^ texel) * 16777619u;
    }
    ++backdrop_clock;
    for (int i = 0; i < BACKDROPS_KEPT; ++i) {
        if (backdrops[i].texture != NULL && backdrops[i].of == one->backdrop && backdrops[i].w == one->backdrop_w &&
            backdrops[i].h == one->backdrop_h && backdrops[i].sample == sample) {
            backdrops[i].used = backdrop_clock;
            return backdrops[i].texture;
        }
        if (backdrops[i].texture == NULL ||
            (backdrops[slot].texture != NULL && backdrops[i].used < backdrops[slot].used)) {
            slot = i; /* an empty one, else the one used longest ago */
        }
    }
    if (backdrops[slot].texture != NULL) {
        SDL_ReleaseGPUTexture(device, backdrops[slot].texture);
    }
    backdrops[slot].of = one->backdrop;
    backdrops[slot].w = one->backdrop_w;
    backdrops[slot].h = one->backdrop_h;
    backdrops[slot].sample = sample;
    backdrops[slot].used = backdrop_clock;
    backdrops[slot].texture = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                           SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                                           one->backdrop_w, one->backdrop_h);
    SDL_GPUTexture *const backdrop_texture = backdrops[slot].texture;
    SDL_zero(info);
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = bytes;
    transfer = backdrop_texture != NULL ? SDL_CreateGPUTransferBuffer(device, &info) : NULL;
    mapped = transfer != NULL ? SDL_MapGPUTransferBuffer(device, transfer, false) : NULL;
    if (mapped == NULL) {
        fprintf(stderr, "one screen: the backdrop (%dx%d) cannot go to the graphics card: %s\n", one->backdrop_w,
                one->backdrop_h, SDL_GetError());
        if (transfer != NULL)
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        if (backdrop_texture != NULL)
            SDL_ReleaseGPUTexture(device, backdrop_texture);
        backdrops[slot].texture = NULL;
        return no_backdrop;
    }
    SDL_memcpy(mapped, one->backdrop, bytes);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    copy = SDL_BeginGPUCopyPass(cmd);
    SDL_zero(src);
    src.transfer_buffer = transfer;
    src.pixels_per_row = (Uint32)one->backdrop_w;
    src.rows_per_layer = (Uint32)one->backdrop_h;
    SDL_zero(dst);
    dst.texture = backdrop_texture;
    dst.w = (Uint32)one->backdrop_w;
    dst.h = (Uint32)one->backdrop_h;
    dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return backdrop_texture;
}

/* Makes sure the one-screen picture's texture exists, (C x N) by (192 x N). */
static int ensure_picture(int columns, int scale)
{
    if (picture != NULL && picture_w == columns * scale && picture_h == KHDAYS_FRAME_H * scale) {
        return 1;
    }
    if (picture_wrapped != NULL) {
        SDL_DestroyTexture(picture_wrapped);
        picture_wrapped = NULL;
        picture_for = NULL;
    }
    if (picture != NULL) {
        SDL_ReleaseGPUTexture(device, picture);
    }
    picture = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                           SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, columns * scale,
                           KHDAYS_FRAME_H * scale);
    picture_w = picture != NULL ? columns * scale : 0;
    picture_h = picture != NULL ? KHDAYS_FRAME_H * scale : 0;
    picture_columns = picture != NULL ? columns : 0;
    return picture != NULL;
}

/* Makes sure a buffer holds at least `need` bytes (doubling; its contents are not kept). */
static int ensure_buffer(SDL_GPUBuffer **buffer, uint32_t *bytes, uint32_t need)
{
    if (need <= *bytes) {
        return 1;
    }
    while (*bytes < need) {
        *bytes *= 2;
    }
    SDL_ReleaseGPUBuffer(device, *buffer);
    *buffer = make_buffer(*bytes);
    return *buffer != NULL;
}

/* The engine on screen `s` of package `p` (0 the upper). */
static int engine_on(const KhdaysFramePackage *p, int s)
{
    return s == 0 ? p->upper : !p->upper;
}

/* The Moogle Shop's screen variant `v` of description `from` (as onescreen.c's shop_desc makes the CPU's, but in 8
 * bits for the HD tiles): not wider, no master brightness unless `fade` (the normal one), no 3D unless `with3d`
 * (the normal one: its 3D at 1x, in 6 bits, so the screen in them too); slid back `slide` dots (its layers'
 * scroll less, its sprites more: the Panels page's); the cut ones without the layers of `without`, no blending,
 * over a black or a white backdrop. */
static void shop_description(const uint32_t *from, uint32_t *to, int v, int fade, unsigned without, int with3d,
                             int slide)
{
    const int keep3d = with3d && v == KHDAYS_SHOP_NORMAL;
    memcpy(to, from, KHDAYS_DESC_WORDS * 4u);
    to[KD_FLAGS] &= ~(uint32_t)KDF_SPLIT;
    if (!keep3d || !(to[KD_FLAGS] & KDF_BG0_IS_3D)) {
        to[KD_FLAGS] |= KDF_FINE;
    }
    to[KD_WIDE] = 0;
    if (v != KHDAYS_SHOP_NORMAL || !fade) {
        to[KD_MASTER] = 0;
    }
    if (v != KHDAYS_SHOP_NORMAL) {
        to[KD_BLEND] = 0;
        to[KD_BACKDROP] = v == KHDAYS_SHOP_CUT_WHITE ? 0x7fffu : 0u;
    }
    for (int l = 0; l < 4; ++l) {
        uint32_t *kind = &to[KD_LAYERS + l * KD_LAYER_WORDS + KDL_KIND];
        if (((*kind & 0xffu) == KDK_3D && !keep3d) || (v != KHDAYS_SHOP_NORMAL && (without & (1u << l)))) {
            *kind = (*kind & ~0xffu) | KDK_NONE;
        }
        if (slide != 0) {
            uint32_t *h = &to[KD_LAYERS + l * KD_LAYER_WORDS + KDL_HOFS];
            *h = (uint32_t)((int)*h - slide) & 511u;
        }
    }
    for (int s = 0; slide != 0 && s < 128; ++s) {
        uint32_t *x = &to[KD_SPRITES + s * KD_SPRITE_WORDS + KDS_X];
        *x = (uint32_t)((int32_t)*x + slide);
    }
}

/* The descriptions' buffers and the textures for the shop's screens wanted (`count` of them, screen and variant),
 * `scale` texels a DS dot; 0 when they cannot be made (the page then shows the CPU's). */
static int ensure_shop(const KhdaysFramePackage *p, int count, const int *screens, const int *variants, int scale)
{
    if (scale != shop_scale) {
        drop_shop_screens();
        shop_scale = scale;
    }
    for (int i = 0; i < count; ++i) {
        const int s = screens[i], v = variants[i];
        const uint32_t need = DESC_BYTES + element_bytes(p, engine_on(p, s));
        if (need > shop_desc_bytes[s][v]) {
            uint32_t bytes = shop_desc_bytes[s][v] != 0 ? shop_desc_bytes[s][v] : DESC_BYTES;
            while (bytes < need) {
                bytes *= 2;
            }
            if (shop_desc[s][v] != NULL) {
                SDL_ReleaseGPUBuffer(device, shop_desc[s][v]);
            }
            shop_desc[s][v] = make_buffer(bytes);
            shop_desc_bytes[s][v] = shop_desc[s][v] != NULL ? bytes : 0;
        }
        if (shop_screen[s][v] == NULL) {
            shop_screen[s][v] = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                             SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                                             KHDAYS_FRAME_W * scale, KHDAYS_FRAME_H * scale);
        }
        if (shop_desc[s][v] == NULL || shop_screen[s][v] == NULL) {
            return 0;
        }
    }
    return 1;
}

static void drop_capture_copy(CaptureCopy *k)
{
    if (k->wrapped != NULL)
        SDL_DestroyTexture(k->wrapped);
    if (k->texture != NULL)
        SDL_ReleaseGPUTexture(device, k->texture);
    SDL_zerop(k);
}

/* The copy engine B's screen shows instead of engine B's picture, or NULL: engine A's picture of when it was on
 * that screen (the one it is not on in the package drawn last), while the capture is in use. */
static CaptureCopy *capture_copy_shown(void)
{
    CaptureCopy *k = &capture_copy[drawn_upper == 0 ? 1 : 0];
    if (!drawn_any || !drawn_capture || !k->ok || k->texture == NULL ||
        !khdays_two_screens_kept_shows(drawn_frame, k->frame, drawn_before, drawn_both)) {
        return NULL;
    }
    return k;
}

/* When a new package comes (not the same one drawn again between two
 * pictures): keep engine A's picture of the previous one, if it was captured
 * (see above). */
static void hold_capture(const KhdaysFramePackage *p)
{
    if (drawn_any && p->frame == drawn_frame) {
        return;
    }
    if (drawn_any && drawn_capture && screen[0] != NULL) {
        /* as it was shown: shrunk to the size shown */
        SDL_GPUTexture *const as_shown = screens_shrunk ? shrunk_screen[0].texture : screen[0];
        const int w = screens_shrunk ? shrunk_screen[0].w : screen_columns[0] * screen_scale;
        const int h = screens_shrunk ? shrunk_screen[0].h : KHDAYS_FRAME_H * screen_scale;
        CaptureCopy *k = &capture_copy[drawn_upper == 0 ? 0 : 1];
        k->ok = 0;
        if (k->texture == NULL || k->w != w || k->h != h) {
            drop_capture_copy(k);
            k->texture = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
            k->w = k->texture != NULL ? w : 0;
            k->h = k->texture != NULL ? h : 0;
        }
        if (k->texture != NULL) {
            SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
            if (cmd != NULL) {
                SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
                SDL_GPUTextureLocation from, to;
                SDL_zero(from);
                SDL_zero(to);
                from.texture = as_shown;
                to.texture = k->texture;
                SDL_CopyGPUTextureToTexture(copy, &from, &to, (Uint32)w, (Uint32)h, 1, false);
                SDL_EndGPUCopyPass(copy);
                k->ok = SDL_SubmitGPUCommandBuffer(cmd);
                k->columns = screen_columns[0];
                k->frame = drawn_frame;
            }
        }
    }
    drawn_before = drawn_any ? drawn_frame : p->frame;
    drawn_frame = p->frame;
    drawn_capture = p->capture;
    /* the game changing screens and taking a capture at this very VBlank: when it stops, engine B's screen is
     * engine B's again at once (a package that stays shown, the game standing still, never grows old) */
    drawn_both = p->both && p->dispcapcnt != 0u;
    drawn_upper = p->upper;
    drawn_any = 1;
    if (trace_capture() && (drawn_capture || drawn_both)) {
        const CaptureCopy *shown_copy = capture_copy_shown();
        fprintf(stderr, "capture: frame %u, engine A on the %s screen%s%s; the other screen shows ", p->frame,
                drawn_upper == 0 ? "upper" : "lower", p->both ? " (the 3D on both)" : "",
                p->dispcapcnt != 0u ? ", a capture armed" : "");
        if (shown_copy != NULL) {
            fprintf(stderr, "the copy of frame %u\n", shown_copy->frame);
        } else {
            fprintf(stderr, "engine B's own picture\n");
        }
    }
}

/* The screens composed whole on one screen too: KHDAYS_GFX=check reads them back whole (gpu_present.c
 * port2d_compare), and KHDAYS_GPU_SKIP=full2d measures what the parts spare. */
static int partial_2d = 1, keep_2d = 1; /* [video] partial_2d, keep_screens */

void khdays_gfx_render_set_savings(int partial, int keep)
{
    partial_2d = partial != 0;
    keep_2d = keep != 0;
}

static int reference; /* khdays_gfx_render_set_reference */

void khdays_gfx_render_set_reference(int on)
{
    reference = on != 0;
}

static int checked_whole(void)
{
    const char *gfx = SDL_getenv("KHDAYS_GFX");
    return (gfx != NULL && SDL_strcmp(gfx, "check") == 0) || gpu_skip("full2d");
}

/* ... or as the settings say (partial_2d off): then they are still composed coarse and kept. */
static int whole_screens(void)
{
    return checked_whole() || !partial_2d || reference;
}

/* The parts of each engine's screen the one-screen picture reads, in DS dots: x0, y0, x1, y1 (x1, y1 past the
 * last). The source rectangles of the shapes that take from a screen (onescreen.h ko_pixel), a dot wider each side
 * for the sampling's rounding, mark the screen's 8 x 8 tiles they touch; each row's runs of marked tiles, joined
 * to the part above when it spans the same columns, are the parts. On the tiles' grid the parts never overlap and
 * are whole dispatch groups (8 x 8 DS pixels in the pre pass, 8 x 8 output pixels in the composition), so no two
 * threads write one texel: Vulkan counts that a race even with the same value. The rest of the screen is not
 * composed: on a one-screen picture that borrows two corners of the lower screen, that was most of its cost
 * (the Tab A9+'s field: 25 ms of a 70 ms frame). */
#define ONE_TILE 8
#define ONE_TILES_X (PRE_COLUMNS / ONE_TILE)
#define ONE_TILES_Y (KHDAYS_FRAME_H / ONE_TILE)
#define ONE_READS 64
typedef struct OneReads {
    int count[2]; /* -1: the screen composed whole (a shape's numbers not usable, or too many parts) */
    int rect[2][ONE_READS][4];
} OneReads;

static int finite_dots(float v)
{
    return SDL_isinff(v) == 0 && SDL_isnanf(v) == 0;
}

/* v clamped to 0..hi, as a float: before it becomes an integer */
static float clamp_dots(float v, float hi)
{
    return v < 0.0f ? 0.0f : v > hi ? hi : v;
}

static void one_reads(const KhdaysOneScreenDraw *one, int upper, const int columns[2], OneReads *out)
{
    static unsigned char marked[2][ONE_TILES_Y][ONE_TILES_X];
    int tiles_x[2];
    SDL_zerop(out);
    SDL_zeroa(marked);
    for (int e = 0; e < 2; ++e) {
        tiles_x[e] = SDL_min((columns[e] + ONE_TILE - 1) / ONE_TILE, ONE_TILES_X);
        if (columns[e] > PRE_COLUMNS || columns[e] <= 0) {
            out->count[e] = -1;
        }
    }
    for (int i = 0; i < one->shape_count; ++i) {
        const uint32_t *w = one->shapes + (size_t)i * KO_WORDS;
        float sx, sy, sw, sh, x1, y1, x2, y2, gx, gy;
        int e;
        if ((w[KO_FLAGS] & (KOF_3D | KOF_IMAGE)) != 0u) {
            continue; /* the 3D, or an image of its own: no screen read */
        }
        memcpy(&sx, &w[KO_SRC_X], 4);
        memcpy(&sy, &w[KO_SRC_Y], 4);
        memcpy(&sw, &w[KO_SRC_W], 4);
        memcpy(&sh, &w[KO_SRC_H], 4);
        memcpy(&x1, &w[KO_DST_X1], 4);
        memcpy(&y1, &w[KO_DST_Y1], 4);
        memcpy(&x2, &w[KO_DST_X2], 4);
        memcpy(&y2, &w[KO_DST_Y2], 4);
        memcpy(&gx, &w[KO_SCALE_X], 4);
        memcpy(&gy, &w[KO_SCALE_Y], 4);
        e = (w[KO_FLAGS] & KOF_LOWER) != 0u ? !upper : upper;
        /* a repeated shape wraps at its destination's length in source dots, which may pass its rectangle; turned
         * over (mirror), the part past it lies before its start: both sides taken */
        if (gx > 0.0f && (x2 - x1) / gx > sw) {
            const float more = (x2 - x1) / gx - sw;
            sx -= more;
            sw += 2.0f * more;
        }
        if (gy > 0.0f && (y2 - y1) / gy > sh) {
            const float more = (y2 - y1) / gy - sh;
            sy -= more;
            sh += 2.0f * more;
        }
        if (!finite_dots(sx) || !finite_dots(sy) || !finite_dots(sx + sw) || !finite_dots(sy + sh) || sw < 0.0f ||
            sh < 0.0f) {
            out->count[e] = -1; /* numbers that say no place: the whole screen */
            continue;
        }
        {
            /* source_at clamps to the screen: a rectangle past its edge reads the edge */
            const float wide = (float)columns[e] - 1.0f, high = (float)KHDAYS_FRAME_H - 1.0f;
            const int c0 = (int)clamp_dots(SDL_floorf(sx) - 1.0f, wide) / ONE_TILE;
            const int r0 = (int)clamp_dots(SDL_floorf(sy) - 1.0f, high) / ONE_TILE;
            const int c1 = (int)clamp_dots(SDL_ceilf(sx + sw) + 1.0f, wide) / ONE_TILE;
            const int r1 = (int)clamp_dots(SDL_ceilf(sy + sh) + 1.0f, high) / ONE_TILE;
            for (int ty = r0; ty <= r1 && ty < ONE_TILES_Y; ++ty) {
                for (int tx = c0; tx <= c1 && tx < tiles_x[e]; ++tx) {
                    marked[e][ty][tx] = 1;
                }
            }
        }
    }
    for (int e = 0; e < 2; ++e) {
        for (int ty = 0; ty < ONE_TILES_Y && out->count[e] >= 0; ++ty) {
            int tx = 0;
            while (tx < tiles_x[e] && out->count[e] >= 0) {
                int end = tx, joined = 0;
                if (!marked[e][ty][tx]) {
                    ++tx;
                    continue;
                }
                while (end < tiles_x[e] && marked[e][ty][end]) {
                    ++end;
                }
                for (int k = 0; k < out->count[e] && !joined; ++k) {
                    int *r = out->rect[e][k];
                    if (r[3] == ty * ONE_TILE && r[0] == tx * ONE_TILE && r[2] == end * ONE_TILE) {
                        r[3] += ONE_TILE; /* the part above, as wide, ends where this run starts: grown down */
                        joined = 1;
                    }
                }
                if (!joined && out->count[e] == ONE_READS) {
                    out->count[e] = -1; /* too many: the whole screen */
                } else if (!joined) {
                    int *r = out->rect[e][out->count[e]++];
                    r[0] = tx * ONE_TILE;
                    r[1] = ty * ONE_TILE;
                    r[2] = end * ONE_TILE;
                    r[3] = (ty + 1) * ONE_TILE;
                }
                tx = end;
            }
        }
    }
}

/* A weaker card composes the one-screen picture's sources a DS pixel at a time (draw). */
static int coarse_2d = -1; /* KHDAYS_COARSE_2D=1 or 0 says it instead, to try it on any card */
static int coarse_pictures[2]; /* KHDAYS_PERF: the pictures each engine's screen was composed so, since it last said */

void khdays_gfx_render_set_coarse_2d(int on)
{
    const char *asked = SDL_getenv("KHDAYS_COARSE_2D");
    coarse_2d = asked != NULL ? SDL_atoi(asked) != 0 : on != 0;
}

static int coarse_now(void)
{
    if (coarse_2d < 0) {
        khdays_gfx_render_set_coarse_2d(0); /* not said: off, but for KHDAYS_COARSE_2D */
    }
    return coarse_2d;
}

/* Whether the frame's 2D has tiles a mod draws instead (runtime/mods/tiles2d.h): graphics finer than the DS's. */
static int finer_than_ds(const KhdaysFramePackage *p)
{
    for (size_t i = 0; i < SDL_arraysize(p->hd2d); ++i) {
        if (p->hd2d[i] != 0u) {
            return 1;
        }
    }
    return 0;
}

/* The one-screen picture's texels in two sets of parts (x0, y0, x1, y1, texels): those a shape can reach, and
 * the rest, which are the 3D or the backdrop alone and need no shape looked at -- most of a field's picture.
 * On the picture's 8 x 8 tiles (whole dispatch groups), each tile in one set, as one_reads' parts. */
#define COVER_TILES_X 512
#define COVER_TILES_Y 160
#define COVER_PARTS 96
typedef struct OneCovers {
    int count[2]; /* [0] the texels a shape can reach, [1] the rest; -1: the picture in one piece */
    int rect[2][COVER_PARTS][4];
} OneCovers;

static void one_covers(const KhdaysOneScreenDraw *one, int scale, int width, int height, OneCovers *out)
{
    static unsigned char marked[COVER_TILES_Y][COVER_TILES_X];
    const int tiles_x = (width + 7) / 8, tiles_y = (height + 7) / 8;
    SDL_zerop(out);
    if (tiles_x > COVER_TILES_X || tiles_y > COVER_TILES_Y || tiles_x <= 0 || tiles_y <= 0) {
        out->count[0] = out->count[1] = -1;
        return;
    }
    for (int ty = 0; ty < tiles_y; ++ty) {
        memset(marked[ty], 0, (size_t)tiles_x);
    }
    for (int i = 0; i < one->shape_count; ++i) {
        const uint32_t *w = one->shapes + (size_t)i * KO_WORDS;
        float x1, y1, x2, y2;
        int c0 = 0, c1 = tiles_x - 1, r0 = 0, r1 = tiles_y - 1;
        memcpy(&x1, &w[KO_DST_X1], 4);
        memcpy(&y1, &w[KO_DST_Y1], 4);
        memcpy(&x2, &w[KO_DST_X2], 4);
        memcpy(&y2, &w[KO_DST_Y2], 4);
        if (!finite_dots(x1) || !finite_dots(y1) || !finite_dots(x2) || !finite_dots(y2)) {
            out->count[0] = out->count[1] = -1; /* numbers that say no place */
            return;
        }
        /* ko_pixel takes the texel whose middle, in DS dots, is within x1..x2 and y1..y2 (a repeated shape:
         * every column, or every row); a texel more each side for the rounding */
        if ((w[KO_FLAGS] & KOF_REPEAT_X) == 0u) {
            c0 = (int)clamp_dots(SDL_floorf(SDL_min(x1, x2) * (float)scale) - 1.0f, (float)(width - 1)) / 8;
            c1 = (int)clamp_dots(SDL_ceilf(SDL_max(x1, x2) * (float)scale) + 1.0f, (float)(width - 1)) / 8;
        }
        if ((w[KO_FLAGS] & KOF_REPEAT_Y) == 0u) {
            r0 = (int)clamp_dots(SDL_floorf(SDL_min(y1, y2) * (float)scale) - 1.0f, (float)(height - 1)) / 8;
            r1 = (int)clamp_dots(SDL_ceilf(SDL_max(y1, y2) * (float)scale) + 1.0f, (float)(height - 1)) / 8;
        }
        for (int ty = r0; ty <= r1; ++ty) {
            memset(&marked[ty][c0], 1, (size_t)(c1 - c0 + 1));
        }
    }
    for (int ty = 0; ty < tiles_y; ++ty) {
        int tx = 0;
        while (tx < tiles_x) {
            const int set = marked[ty][tx] ? 0 : 1;
            int end = tx, joined = 0;
            while (end < tiles_x && (marked[ty][end] ? 0 : 1) == set) {
                ++end;
            }
            for (int k = 0; k < out->count[set] && !joined; ++k) {
                int *r = out->rect[set][k];
                if (r[3] == ty * 8 && r[0] == tx * 8 && r[2] == end * 8) {
                    r[3] += 8; /* the part above, as wide, ends where this run starts: grown down */
                    joined = 1;
                }
            }
            if (!joined && out->count[set] == COVER_PARTS) {
                out->count[0] = out->count[1] = -1; /* too many: in one piece */
                return;
            } else if (!joined) {
                int *r = out->rect[set][out->count[set]++];
                r[0] = tx * 8;
                r[1] = ty * 8;
                r[2] = end * 8;
                r[3] = (ty + 1) * 8;
            }
            tx = end;
        }
    }
}

/* A screen composed a DS pixel at a time (coarse_now), but where an element is drawn finer than the DS's pixels
 * or between them (the port's own text): the parts' 8 x 8 tiles such an element touches are composed at each
 * output pixel, as without it, and the rest a DS pixel at a time. The parts in two sets (x0, y0, x1, y1, DS
 * dots), each tile in one; -1 when they are too many, and the screen is then composed at each output pixel. */
#define FINE_PARTS 96
typedef struct FineParts {
    int count[2]; /* [0] a DS pixel at a time, [1] each output pixel */
    int rect[2][FINE_PARTS][4];
} FineParts;

static void fine_parts(const KhdaysFramePackage *p, int e, int split, int (*parts)[4], int part_count, int columns,
                       FineParts *out)
{
    static unsigned char marked[ONE_TILES_Y][ONE_TILES_X];
    const int tiles_x = (columns + ONE_TILE - 1) / ONE_TILE;
    SDL_zerop(out);
    if (tiles_x > ONE_TILES_X || tiles_x <= 0) {
        out->count[0] = out->count[1] = -1;
        return;
    }
    memset(marked, 0, sizeof(marked));
    for (uint32_t i = 0; i < p->elements[e].count; ++i) {
        const uint32_t *w = p->elements[e].records + (size_t)i * KE_WORDS;
        float x, y, ew, eh;
        int c0, c1, r0, r1;
        memcpy(&x, &w[KE_X], 4);
        memcpy(&y, &w[KE_Y], 4);
        memcpy(&ew, &w[KE_W], 4);
        memcpy(&eh, &w[KE_H], 4);
        if (w[KE_IW] == 0u || w[KE_IH] == 0u || (split && (w[KE_SLOT] & KES_PICTURE) != 0u) ||
            (w[KE_SLOT] & KES_OVERLAY) != 0u) {
            continue; /* not drawn here (compose.h kd_element_texel) */
        }
        if (!finite_dots(x) || !finite_dots(y) || !finite_dots(ew) || !finite_dots(eh)) {
            out->count[0] = out->count[1] = -1; /* numbers that say no place */
            return;
        }
        if ((float)w[KE_IW] == ew && (float)w[KE_IH] == eh && x == SDL_floorf(x) && y == SDL_floorf(y)) {
            continue; /* a texel a DS pixel, on the DS's pixels: the same all over each */
        }
        if (x + ew <= 0.0f || y + eh <= 0.0f || x >= (float)columns || y >= (float)KHDAYS_FRAME_H) {
            continue;
        }
        c0 = (int)clamp_dots(SDL_floorf(x), (float)(columns - 1)) / ONE_TILE;
        c1 = (int)clamp_dots(SDL_ceilf(x + ew), (float)(columns - 1)) / ONE_TILE;
        r0 = (int)clamp_dots(SDL_floorf(y), (float)(KHDAYS_FRAME_H - 1)) / ONE_TILE;
        r1 = (int)clamp_dots(SDL_ceilf(y + eh), (float)(KHDAYS_FRAME_H - 1)) / ONE_TILE;
        for (int ty = r0; ty <= r1; ++ty) {
            memset(&marked[ty][c0], 1, (size_t)(c1 - c0 + 1));
        }
    }
    for (int k = 0; k < part_count; ++k) {
        /* each part's tiles by rows, a run of one set joined to the part above it when as wide */
        const int first[2] = {out->count[0], out->count[1]};
        for (int y0 = parts[k][1]; y0 < parts[k][3]; y0 += ONE_TILE) {
            const int y1 = SDL_min(y0 + ONE_TILE, parts[k][3]);
            int x0 = parts[k][0];
            while (x0 < parts[k][2]) {
                const int set = marked[SDL_min(y0 / ONE_TILE, ONE_TILES_Y - 1)][SDL_min(x0 / ONE_TILE, tiles_x - 1)];
                int x1 = x0, joined = 0;
                while (x1 < parts[k][2] &&
                       marked[SDL_min(y0 / ONE_TILE, ONE_TILES_Y - 1)][SDL_min(x1 / ONE_TILE, tiles_x - 1)] == set) {
                    x1 = SDL_min((x1 / ONE_TILE + 1) * ONE_TILE, parts[k][2]);
                }
                for (int j = first[set]; j < out->count[set] && !joined; ++j) {
                    int *r = out->rect[set][j];
                    if (r[3] == y0 && r[0] == x0 && r[2] == x1) {
                        r[3] = y1;
                        joined = 1;
                    }
                }
                if (!joined && out->count[set] == FINE_PARTS) {
                    out->count[0] = out->count[1] = -1;
                    return;
                } else if (!joined) {
                    int *r = out->rect[set][out->count[set]++];
                    r[0] = x0;
                    r[1] = y0;
                    r[2] = x1;
                    r[3] = y1;
                }
                x0 = x1;
            }
        }
    }
}

/* A screen kept from the picture before: an engine that reads no 3D gives the same screen when what the card
 * is sent for it (its description, palettes, elements and sprites: `sent`), its memory (a page of its areas
 * changed since: `stale`), the replacements' buffer, its parts and its composition's parameters are the same.
 * Its passes are then left out whole -- the screen's texture is not bound, so not made anew -- and what reads
 * it finds what the last composition left. A menu that stands still, the lower screen under a field, and the
 * pictures drawn between two game frames: on a Tab A9+ a screen's passes cost 4 to 7 ms of a 20 ms picture.
 * Compared whole, not by a hash: a screen wrongly kept would stay wrong. Not while measuring or checking
 * (KHDAYS_GPU_SKIP, KHDAYS_GPU_TIME, KHDAYS_GFX=check), and KHDAYS_KEEP_2D=0 composes every time. */
typedef struct KeptHow {
    Compose2dParams cp;
    int parts[ONE_READS][4];
    int part_count, how, coarse;
    uint32_t hd_changes;
    SDL_GPUComputePipeline *pipeline;
    SDL_GPUTexture *texture;
} KeptHow;

static struct {
    int ok, stale;
    uint8_t *sent;
    uint32_t sent_bytes, sent_room;
    KeptHow how;
} kept[2];

static void forget_kept(void)
{
    kept[0].ok = kept[1].ok = 0;
}

/* What the card is sent for engine e this picture: its screen is stale when that differs from the last. */
static void kept_sent(int e, const uint8_t *bytes, uint32_t n)
{
    if (kept[e].sent_bytes == n && kept[e].sent != NULL && memcmp(kept[e].sent, bytes, n) == 0) {
        return;
    }
    kept[e].stale = 1;
    if (n > kept[e].sent_room) {
        uint8_t *grown = (uint8_t *)SDL_realloc(kept[e].sent, n);
        if (grown == NULL) {
            kept[e].sent_bytes = 0; /* not kept: never the same */
            return;
        }
        kept[e].sent = grown;
        kept[e].sent_room = n;
    }
    if (kept[e].sent == NULL) {
        kept[e].sent_bytes = 0; /* (nothing sent, nothing to keep it by) */
        return;
    }
    memcpy(kept[e].sent, bytes, n);
    kept[e].sent_bytes = n;
}

static int kept_screen(int e, const Compose2dParams *cp, int (*parts)[4], int part_count, int how)
{
    static int asked = -1, perf = -1, pictures[2], hits[2];
    KeptHow now;
    int hit;
    if (reference) {
        return 0; /* composed anew; what is kept is as good after it (the same screen, whole) */
    }
    if (asked < 0) {
        const char *keep = SDL_getenv("KHDAYS_KEEP_2D");
        asked = keep == NULL || SDL_atoi(keep) != 0;
        perf = SDL_getenv("KHDAYS_PERF") != NULL;
    }
    if (!asked || cp->source3d != 0u || part_count > ONE_READS || kept[e].sent_bytes == 0 || !keep_2d || checked_whole() ||
        SDL_getenv("KHDAYS_GPU_SKIP") != NULL || SDL_getenv("KHDAYS_GPU_TIME") != NULL) {
        kept[e].ok = 0;
        return 0;
    }
    SDL_zero(now); /* (its padding too: compared by its bytes) */
    now.cp = *cp;
    memcpy(now.parts, parts, sizeof(parts[0]) * (size_t)part_count);
    now.part_count = part_count;
    now.how = how;
    now.coarse = coarse_now();
    now.hd_changes = khdays_gpu_hd_changes();
    now.pipeline = compose_engine[e];
    now.texture = screen[e];
    hit = kept[e].ok && !kept[e].stale && memcmp(&kept[e].how, &now, sizeof(now)) == 0;
    hits[e] += hit;
    if (perf && ++pictures[e] == 300) {
        /* KHDAYS_PERF: how often each screen was kept */
        fprintf(stderr, "host: engine %c's screen kept %d times in 300%c", e == 0 ? 'A' : 'B', hits[e], 10);
        pictures[e] = hits[e] = 0;
    }
    if (hit) {
        return 1;
    }
    kept[e].ok = 1;
    kept[e].stale = 0;
    kept[e].how = now;
    return 0;
}

/* The 3D read between its texels where the 2D is drawn finer than it (khdays_gfx_render_set_smooth_3d). */
static int smooth3d;

static int draw(const KhdaysFramePackage *p, int scale, int scale3d, int raster, const KhdaysOneScreenDraw *one)
{
    const int binned = one != NULL && one->animated && one->piece_count > 0 &&
                       bin_pieces(one->pieces, one->piece_count, one->picture_w > 0 ? one->picture_w : one->backdrop_w,
                                  one->picture_h > 0 ? one->picture_h : one->backdrop_h);
    const uint32_t bin_bytes = binned ? bins_words * 4u : 0u;
    int changed[KHDAYS_FRAME_2D_PAGES], nchanged = 0;
    int hd_changed[KHDAYS_FRAME_HD2D_PAGES], nhd = 0;
    const int has3d = (p->wants & KHDAYS_WANT_3D) != 0;
    const uint32_t fixed = 2u * KHDAYS_DESC_WORDS * 4u + 2u * 0x400u + (has3d ? KHDAYS_FRAME_PIXELS * 4u : 0u) +
                           element_bytes(p, 0) + element_bytes(p, 1) +
                           (one != NULL ? (uint32_t)one->shape_count * KO_WORDS * 4u + (uint32_t)one->key_count * 4u +
                                              (uint32_t)one->piece_count * PIECE_BYTES + bin_bytes
                                        : 0u);
    uint32_t size, at;
    uint8_t *mapped;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture *color3d = NULL, *attr3d = NULL;
    int raster_columns = 0;
    /* Engine A is wider than the DS's only when its 3D is drawn as wide. For the one-screen picture its 2D
     * stays at the DS's 256 columns, apart from the 3D drawn as wide as the picture. */
    uint32_t wide = raster && one == NULL ? p->desc2d[0][KD_WIDE] : 0;
    /* the one-screen picture showing engine A, the lower screen, whole with its 3D (the story's Panels page:
     * onescreen.c camp_cards): its 2D composed with the 3D, as two screens are, not apart from it */
    const int whole_a = one != NULL && one->lower_whole && p->upper != 0;
    int columns_a;
    const uint32_t shape_bytes = one != NULL ? (uint32_t)one->shape_count * KO_WORDS * 4u : 0u;
    const uint32_t key_bytes = one != NULL ? (uint32_t)one->key_count * 4u : 0u;
    const uint32_t piece_bytes =
        one != NULL ? (uint32_t)one->piece_count * PIECE_BYTES + bin_bytes : 0u; /* and the bins */
    /* the Moogle Shop's screens wanted apart (khdays_gfx_render_shop): each one's screen and variant, their
     * descriptions' bytes, and where each engine's description is in the upload */
    int shop_count = 0, shop_s[2 * KHDAYS_SHOP_VARIANTS], shop_v[2 * KHDAYS_SHOP_VARIANTS];
    uint32_t shop_bytes, engine_at[2] = {0, 0}, shop_tail_at[2 * KHDAYS_SHOP_VARIANTS];
    /* a screen blend (take_capture); while engine A shows a bank, it is composed as its layers and 3D,
     * as the capture takes it */
    CapturePlan plan;
    int blend, layers;
    plan_capture(p, &plan);
    blend = raster && (plan.show >= 0 || plan.capture >= 0);
    layers = raster && plan_layers(p, &plan);
    if (trace_capture() && (p->desc2d[0][KD_FLAGS] & KDF_DISPLAY_MASK) == 2u && !blend) {
        fprintf(stderr, "capture: frame %u, engine A shows bank %u at the DS's size (%s)\n", p->frame,
                (unsigned)(p->desc2d[0][KD_VRAM] & 3u), raster ? "no current copy of it" : "no 3D drawn");
    }
    if (device != NULL && (p->wants & KHDAYS_WANT_2D)) {
        hold_capture(p); /* before this package's pictures replace the previous ones */
    }
    if (raster && !khdays_raster3d_targets(&color3d, &attr3d, &raster_columns)) {
        SDL_SetError("the 3D at that resolution is not drawn");
        return 0;
    }
    if (layers && one == NULL && raster_columns > KHDAYS_FRAME_W) {
        wide = (uint32_t)(raster_columns - KHDAYS_FRAME_W) / 2u; /* as wide as its 3D, no HUD piece moved */
    }
    columns_a = KHDAYS_FRAME_W + 2 * (int)wide;
    if (raster && raster_columns != (one != NULL ? one->columns : columns_a)) {
        SDL_SetError("the 3D is not as wide as the 2D's description");
        return 0;
    }
    if (raster && (scale3d < 1 || scale3d > scale)) {
        SDL_SetError("the 3D is drawn finer than the 2D");
        return 0;
    }
    /* the HUD's 3D drawn apart at the 2D's scale (gpu_raster3d.h), when the rest is coarser */
    SDL_GPUTexture *hud_color = NULL, *hud_attr = NULL;
    int hud_columns = 0;
    const int hud = raster && scale3d < scale && khdays_raster3d_hud_targets(&hud_color, &hud_attr, &hud_columns) &&
                    hud_columns == raster_columns;
    if (device == NULL || !(p->wants & KHDAYS_WANT_2D) || !screens(scale, columns_a)) {
        if (device != NULL && !(p->wants & KHDAYS_WANT_2D)) {
            SDL_SetError("the package has no 2D descriptions");
        }
        return 0;
    }
    if (one != NULL &&
        (!ensure_buffer(&shape_buffer, &shape_buffer_bytes, shape_bytes) ||
         !ensure_buffer(&key_buffer, &key_buffer_bytes, key_bytes) ||
         !ensure_buffer(&piece_buffer, &piece_buffer_bytes, piece_bytes) || !ensure_picture(one->columns, scale))) {
        valid = 0;
        return 0;
    }
    if (one != NULL) {
        for (int s = 0; s < 2; ++s) {
            for (int v = 0; v < KHDAYS_SHOP_VARIANTS; ++v) {
                if (one->shop_screens[s] & (v == KHDAYS_SHOP_NORMAL ? KHDAYS_SHOP_WANT_NORMAL : KHDAYS_SHOP_WANT_CUT)) {
                    shop_s[shop_count] = s;
                    shop_v[shop_count++] = v;
                }
            }
        }
        if (shop_count != 0 &&
            !ensure_shop(p, shop_count, shop_s, shop_v,
                         one->shop_scale >= 1 && one->shop_scale <= KHDAYS_SHOP_SCALE ? one->shop_scale
                                                                                      : KHDAYS_SHOP_SCALE)) {
            drop_shop_screens(); /* none: the page shows the CPU's */
            shop_count = 0;
        }
    }
    shop_bytes = (uint32_t)shop_count * KHDAYS_DESC_WORDS * 4u;
    for (int i = 0; i < shop_count; ++i) {
        if (one->shop_slide[shop_s[i]] != 0) {
            /* a slid screen's own palettes and elements, moved with its layers */
            shop_bytes += DESC_BYTES - KHDAYS_DESC_WORDS * 4u + element_bytes(p, engine_on(p, shop_s[i]));
        }
    }
    for (int e = 0; e < 2; ++e) {
        /* make room for this frame's elements after the description and its palettes */
        const uint32_t need = DESC_BYTES + element_bytes(p, e);
        if (need > desc_bytes[e]) {
            uint32_t bytes = desc_bytes[e] != 0 ? desc_bytes[e] : need; /* none after a failed one */
            while (bytes < need)
                bytes *= 2;
            SDL_ReleaseGPUBuffer(device, desc[e]);
            desc[e] = make_buffer(bytes);
            desc_bytes[e] = desc[e] != NULL ? bytes : 0;
            if (desc[e] == NULL) {
                valid = 0;
                return 0;
            }
        }
    }
    for (int n = 0; n < KHDAYS_FRAME_2D_PAGES; ++n) {
        if (!valid || version[n] != p->mem2d_version[n]) {
            changed[nchanged++] = n;
            version[n] = p->mem2d_version[n];
        }
    }
    for (int n = 0; n < KHDAYS_FRAME_HD2D_PAGES; ++n) {
        if (!valid || hd2d_version[n] != p->hd2d_version[n]) {
            hd_changed[nhd++] = n;
            hd2d_version[n] = p->hd2d_version[n];
        }
    }
    for (int k = 0; k < nchanged + nhd; ++k) {
        /* whose memory changed since its screen was composed (kept_screen): an engine's own areas, the bank
         * a VRAM display shows for both */
        uint32_t offset;
        const int a = area_of(k < nchanged ? changed[k] : hd_changed[k - nchanged], &offset);
        const int of_a = a == ABG || a == AOBJ || a == ABGEXT || a == AOBJEXT;
        const int of_b = a == BBG || a == BOBJ || a == BBGEXT || a == BOBJEXT;
        kept[0].stale |= !of_b;
        kept[1].stale |= !of_a;
    }
    valid = 1;
    size = fixed + shop_bytes + (uint32_t)nchanged * PAGE + (uint32_t)nhd * HD2D_PAGE;
    if (size > upload_size) {
        SDL_GPUTransferBufferCreateInfo info;
        if (upload != NULL) {
            SDL_ReleaseGPUTransferBuffer(device, upload);
        }
        SDL_zero(info);
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = size + size / 2;
        upload = SDL_CreateGPUTransferBuffer(device, &info);
        upload_size = upload != NULL ? info.size : 0;
        if (upload == NULL) {
            valid = 0;
            return 0;
        }
    }
    mapped = (uint8_t *)SDL_MapGPUTransferBuffer(device, upload, true);
    if (mapped == NULL) {
        valid = 0;
        return 0;
    }
    at = 0;
    int drop_elements = 0;
    for (int e = 0; e < 2; ++e) {
        engine_at[e] = at;
        memcpy(mapped + at, p->desc2d[e], KHDAYS_DESC_WORDS * 4);
        if (e == 0) {
            ((uint32_t *)(mapped + at))[KD_WIDE] = wide; /* 0: no wider 3D this frame, the DS's columns */
        }
        if (e == 0 && layers) {
            uint32_t *flags = &((uint32_t *)(mapped + at))[KD_FLAGS];
            *flags = (*flags & ~KDF_DISPLAY_MASK) | 1u;
        }
        if ((khdays_raster3d_fine() || (p->wants & KHDAYS_WANT_MOVIE)) &&
            (e == 1 || raster || !(((const uint32_t *)(mapped + at))[KD_FLAGS] & KDF_BG0_IS_3D))) {
            /* compose in 8 bits per channel (HD graphics, a movie's picture): for engine
             * A only when its 3D is drawn in 8 bits too, or it has no 3D */
            ((uint32_t *)(mapped + at))[KD_FLAGS] |= KDF_FINE;
        }
        if (SDL_getenv("KHDAYS_GPU_DROP") != NULL) {
            /* KHDAYS_GPU_DROP=bg0,bg1,bg2,bg3,el: left out of the description, to measure their cost */
            const char *drop = SDL_getenv("KHDAYS_GPU_DROP");
            uint32_t *d = (uint32_t *)(mapped + at);
            for (int b = 0; b < 4; ++b) {
                const char name[4] = {'b', 'g', (char)('0' + b), 0};
                if (SDL_strstr(drop, name) != NULL) {
                    d[KD_FLAGS] &= ~(1u << (KDF_BG_ON_SHIFT + b));
                }
            }
            if (SDL_strstr(drop, "obj") != NULL) {
                d[KD_FLAGS] &= ~KDF_OBJ_ON;
            }
            if (SDL_strstr(drop, "blend") != NULL) {
                d[KD_BLEND] = 0;
            }
            if (SDL_strstr(drop, "win") != NULL) {
                d[KD_FLAGS] &= ~(7u << KDF_WIN_ON_SHIFT);
            }
            drop_elements = SDL_strstr(drop, "el") != NULL; /* their records emptied below: the tail stays put */
        }
        if (one != NULL && e == 0 && whole_a) {
            /* engine A whole, its 3D in it (whole_a), without the master brightness: the port's screen draws the
             * game's fade itself */
            ((uint32_t *)(mapped + at))[KD_MASTER] = 0;
        } else if (one != NULL) {
            /* the one-screen picture's sources: the 2D apart from the 3D, in 8 bits, no master brightness */
            ((uint32_t *)(mapped + at))[KD_FLAGS] |= KDF_SPLIT | KDF_FINE;
        }
        compose_engine[e] =
            compose_for(((const uint32_t *)(mapped + at))[KD_WIDE], ((const uint32_t *)(mapped + at))[KD_FLAGS]);
        at += KHDAYS_DESC_WORDS * 4;
        memcpy(mapped + at, p->palette2d + 0x400 * e, 0x400);
        at += 0x400;
        if (p->elements[e].count != 0) {
            memcpy(mapped + at, p->elements[e].records, p->elements[e].count * KE_WORDS * 4u);
            for (uint32_t i = 0; drop_elements && i < p->elements[e].count; ++i) {
                ((uint32_t *)(mapped + at))[i * KE_WORDS + KE_IW] = 0;
            }
            at += p->elements[e].count * KE_WORDS * 4u;
        }
        if (p->desc2d[e][KD_LEFT_OUT] != 0) {
            memcpy(mapped + at, p->left_out_cells[e], KE_LEFT_CELL_WORDS * 4u);
            at += KE_LEFT_CELL_WORDS * 4u;
            memcpy(mapped + at, p->left_out_obj[e], KE_LEFT_OBJ_WORDS * 4u);
            at += KE_LEFT_OBJ_WORDS * 4u;
        }
        sprite_rows(p->desc2d[e], (uint32_t *)(mapped + at));
        at += SPRITE_ROW_BYTES;
        kept_sent(e, mapped + engine_at[e], at - engine_at[e]);
    }
    if (has3d) {
        memcpy(mapped + at, p->layer3d, KHDAYS_FRAME_PIXELS * 4);
        at += KHDAYS_FRAME_PIXELS * 4;
    }
    if (one != NULL) {
        memcpy(mapped + at, one->shapes, shape_bytes);
        at += shape_bytes;
        memcpy(mapped + at, one->keys, key_bytes);
        at += key_bytes;
        if (piece_bytes != 0) {
            memcpy(mapped + at, one->pieces, piece_bytes - bin_bytes);
            memcpy(mapped + at + piece_bytes - bin_bytes, bins, bin_bytes);
        }
        at += piece_bytes;
        for (int i = 0; i < shop_count; ++i) {
            const int e = engine_on(p, shop_s[i]), slide = one->shop_slide[shop_s[i]];
            shop_description(p->desc2d[e], (uint32_t *)(mapped + at), shop_v[i], one->shop_fade[shop_s[i]],
                             one->shop_cut_layers[shop_s[i]], one->shop_with3d[shop_s[i]], slide);
            at += KHDAYS_DESC_WORDS * 4u;
            shop_tail_at[i] = 0;
            if (slide != 0) {
                /* the elements (0.1.4's HD text: places on the screen as it is) slid back with the layers and
                 * sprites, or the words of the view shown stay over the other's */
                const uint32_t tail = DESC_BYTES - KHDAYS_DESC_WORDS * 4u + element_bytes(p, e);
                uint8_t *records = mapped + at + 0x400u;
                memcpy(mapped + at, mapped + engine_at[e] + KHDAYS_DESC_WORDS * 4u, tail);
                for (uint32_t k = 0; k < p->elements[e].count; ++k) {
                    float x;
                    memcpy(&x, records + (k * KE_WORDS + KE_X) * 4u, 4);
                    x += (float)slide;
                    memcpy(records + (k * KE_WORDS + KE_X) * 4u, &x, 4);
                }
                shop_tail_at[i] = at;
                at += tail;
            }
        }
    }
    for (int k = 0; k < nchanged; ++k) {
        memcpy(mapped + at + (uint32_t)k * PAGE, p->mem2d + (size_t)changed[k] * PAGE, PAGE);
    }
    for (int k = 0; k < nhd; ++k) {
        memcpy(mapped + at + (uint32_t)nchanged * PAGE + (uint32_t)k * HD2D_PAGE,
               p->hd2d + (size_t)hd_changed[k] * KHDAYS_FRAME_HD2D_PAGE_WORDS, HD2D_PAGE);
    }
    SDL_UnmapGPUTransferBuffer(device, upload);

    cmd = SDL_AcquireGPUCommandBuffer(device);
    if (cmd == NULL) {
        valid = 0;
        return 0;
    }
    if (!khdays_gpu_hd_sync(cmd)) { /* upload the HD replacements the card does not have yet */
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    if (!khdays_sequence_gpu_draw(device, cmd, &p->sequence_image)) {
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    if (!khdays_sequence_gpu_draw(device, cmd, &p->enemy_image)) {
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    for (int i = 0; i < 2; ++i) {
        if (!khdays_sequence_gpu_draw(device, cmd, &p->information_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (!khdays_sequence_gpu_draw(device, cmd, &p->minimap_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    }
    for (int i = 0; i < 3; ++i)
        if (!khdays_sequence_gpu_draw(device, cmd, &p->notification_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    if (one && !khdays_sequence_gpu_draw(device, cmd, &p->radar_image)) {
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    if (!khdays_sequence_gpu_draw(device, cmd, &p->mission_panel_image)) {
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    if (!khdays_sequence_gpu_draw(device, cmd, &p->obtained_image)) {
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        if (!khdays_sequence_gpu_draw(device, cmd, &p->party_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (!khdays_sequence_gpu_draw(device, cmd, &p->portrait_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    }
    for (int i = 0; i < 5; ++i) {
        if (!khdays_sequence_gpu_draw(device, cmd, &p->label_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    }
    {
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src;
        SDL_GPUBufferRegion dst;
        src.transfer_buffer = upload;
        at = 0;
        for (int e = 0; e < 2; ++e) {
            src.offset = at;
            dst.buffer = desc[e];
            dst.offset = 0;
            dst.size = DESC_BYTES + element_bytes(p, e); /* the description, its palettes and its elements */
            SDL_UploadToGPUBuffer(copy, &src, &dst, true);
            at += DESC_BYTES + element_bytes(p, e);
        }
        if (has3d) {
            src.offset = at;
            dst.buffer = layer3d;
            dst.offset = 0;
            dst.size = KHDAYS_FRAME_PIXELS * 4;
            SDL_UploadToGPUBuffer(copy, &src, &dst, true);
            at += KHDAYS_FRAME_PIXELS * 4;
        }
        if (one != NULL) {
            if (shape_bytes != 0) {
                src.offset = at;
                dst.buffer = shape_buffer;
                dst.offset = 0;
                dst.size = shape_bytes;
                SDL_UploadToGPUBuffer(copy, &src, &dst, true);
            }
            at += shape_bytes;
            if (key_bytes != 0) {
                src.offset = at;
                dst.buffer = key_buffer;
                dst.offset = 0;
                dst.size = key_bytes;
                SDL_UploadToGPUBuffer(copy, &src, &dst, true);
            }
            at += key_bytes;
            if (piece_bytes != 0) {
                src.offset = at;
                dst.buffer = piece_buffer;
                dst.offset = 0;
                dst.size = piece_bytes;
                SDL_UploadToGPUBuffer(copy, &src, &dst, true);
            }
            at += piece_bytes;
            for (int i = 0; i < shop_count; ++i) {
                /* the shop's description, then its engine's palettes and elements (after that engine's own,
                 * not cycled again: the first part stays) */
                const int e = engine_on(p, shop_s[i]);
                src.offset = at;
                dst.buffer = shop_desc[shop_s[i]][shop_v[i]];
                dst.offset = 0;
                dst.size = KHDAYS_DESC_WORDS * 4u;
                SDL_UploadToGPUBuffer(copy, &src, &dst, true);
                src.offset = shop_tail_at[i] != 0 ? shop_tail_at[i] : engine_at[e] + KHDAYS_DESC_WORDS * 4u;
                dst.offset = KHDAYS_DESC_WORDS * 4u;
                dst.size = DESC_BYTES - KHDAYS_DESC_WORDS * 4u + element_bytes(p, e);
                SDL_UploadToGPUBuffer(copy, &src, &dst, false);
                at += KHDAYS_DESC_WORDS * 4u + (shop_tail_at[i] != 0 ? dst.size : 0u);
            }
        }
        for (int k = 0; k < nchanged; ++k) {
            /* the pages that changed, into the card's copy of the memory */
            uint32_t offset;
            const int a = area_of(changed[k], &offset);
            if (a < 0) {
                continue;
            }
            src.offset = at + (uint32_t)k * PAGE;
            dst.buffer = memory[a];
            dst.offset = offset;
            dst.size = PAGE;
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        }
        for (int k = 0; k < nhd; ++k) {
            /* the map pages that changed, after their area's memory */
            uint32_t offset;
            const int a = area_of(hd_changed[k], &offset);
            if (a < 0 || a > BOBJ) {
                continue;
            }
            src.offset = at + (uint32_t)nchanged * PAGE + (uint32_t)k * HD2D_PAGE;
            dst.buffer = memory[a];
            dst.offset = (uint32_t)area[a].pages * PAGE + offset / PAGE * HD2D_PAGE;
            dst.size = HD2D_PAGE;
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        }
        SDL_EndGPUCopyPass(copy);
    }
    /* the one-screen picture reads the screens only through its shapes (one_reads): without any (an HD picture
     * alone) they are not composed, but where kept whole (the capture) */
    OneReads reads;
    if (one != NULL) {
        one_reads(one, p->upper, screen_columns, &reads);
    }
    for (int e = 0; e < 2; ++e) {
        Compose2dParams cp;
        SDL_GPUStorageTextureReadWriteBinding io[7];
        SDL_GPUBuffer *in[8];
        SDL_GPUComputePass *pass;
        const int b = e == 1;
        /* the parts composed, DS pixels: the whole screen, or on one screen what its shapes read (one_reads). Engine
         * A whole when it is shown whole, or its screen is kept as the capture (hold_capture) */
        int whole[1][4] = {{0, 0, screen_columns[e], KHDAYS_FRAME_H}};
        int (*parts)[4] = whole;
        int part_count = 1;
        if (one != NULL && !(e == 0 && (whole_a || p->capture)) && !whole_screens()) {
            if (reads.count[e] == 0) {
                continue; /* nothing of this screen shown */
            }
            if (reads.count[e] > 0) {
                parts = reads.rect[e];
                part_count = reads.count[e];
            }
        }
        {
            /* KHDAYS_PERF: how much of each screen is composed, said every 300 pictures */
            static int asked = -1, pictures[2], part_sum[2], sprite_sum[2], element_sum[2];
            static long dots[2];
            if (asked < 0) {
                asked = SDL_getenv("KHDAYS_PERF") != NULL;
            }
            if (asked) {
                for (int k = 0; k < part_count; ++k) {
                    dots[e] += (long)(parts[k][2] - parts[k][0]) * (parts[k][3] - parts[k][1]);
                }
                part_sum[e] += part_count;
                element_sum[e] += (int)p->elements[e].count;
                for (int i = 0; i < 128; ++i) {
                    sprite_sum[e] += (p->desc2d[e][KD_SPRITES + (uint32_t)i * KD_SPRITE_WORDS + KDS_SIZE] & KDS_SHOWN) != 0u;
                }
                if (++pictures[e] == 300) {
                    fprintf(stderr,
                            "host: engine %c composed %ld of %d dots a picture in %d parts at %dx (%s%s), %d sprites, "
                            "%d elements, flags %08x; a DS pixel at a time (fast 2D) in %d of 300 pictures%s\n",
                            e == 0 ? 'A' : 'B', dots[e] / 300, screen_columns[e] * KHDAYS_FRAME_H, part_sum[e] / 300,
                            scale, one == NULL ? "two screens" : "one screen", e == 0 && whole_a ? ", whole" :
                            e == 0 && p->capture ? ", the capture" : "", sprite_sum[e] / 300, element_sum[e] / 300,
                            (unsigned)p->desc2d[e][KD_FLAGS], coarse_pictures[e],
                            !coarse_now() ? " (the setting is off)" : finer_than_ds(p) ? " (a mod's tiles now)" : "");
                    pictures[e] = part_sum[e] = sprite_sum[e] = element_sum[e] = coarse_pictures[e] = 0;
                    dots[e] = 0;
                }
            }
        }
        SDL_zero(cp);
        cp.scale = (uint32_t)scale;
        cp.width = (uint32_t)(screen_columns[e] * scale);
        cp.height = (uint32_t)(KHDAYS_FRAME_H * scale);
        /* The one-screen picture's 2D (composed apart from its 3D) reads the 3D
         * drawn as wide as the picture, with the DS's 256 columns in its middle,
         * to know where the game's 3D is transparent (kd_split). */
        cp.source3d = e != 0 ? 0u : raster ? 2u : one != NULL && !whole_a ? 0u : has3d ? 1u : 0u;
        cp.offset3d = e == 0 && raster && one != NULL ? (uint32_t)(raster_columns - KHDAYS_FRAME_W) / 2u : 0u;
        cp.scale3d = e == 0 && raster ? (uint32_t)scale3d : (uint32_t)scale;
        cp.width3d = e == 0 && raster ? (uint32_t)(raster_columns * scale3d) : cp.width;
        cp.height3d = e == 0 && raster ? (uint32_t)(KHDAYS_FRAME_H * scale3d) : cp.height;
        cp.smooth3d = (uint32_t)smooth3d;
        cp.hud3d = (uint32_t)(e == 0 && hud);
        cp.hud_width = (uint32_t)(raster_columns * scale);
        cp.hud_height = (uint32_t)(KHDAYS_FRAME_H * scale);
        if (e == 0 && raster) {
            const uint8_t *io3d = p->io3d;
            cp.disp3dcnt = p->disp3dcnt;
            cp.swap_params = p->list3d.swap_params;
            cp.fine = (uint32_t)khdays_raster3d_fine();
            cp.clear_color = io32(io3d, 0x50);
            cp.clear_depth = io32(io3d, 0x54);
            cp.fog_color = io32(io3d, 0x58);
            cp.fog_offset = io32(io3d, 0x5c);
            for (int i = 0; i < 4; ++i) {
                cp.edge_colors[i] = io32(io3d, 0x30 + 4u * (unsigned)i);
            }
            for (int i = 0; i < 8; ++i) {
                cp.fog_table[i] = io32(io3d, 0x60 + 4u * (unsigned)i);
            }
        }
        if (kept_screen(e, &cp, parts, part_count,
                        (one != NULL) | whole_a << 1 | (p->capture != 0) << 2 | finer_than_ds(p) << 3)) {
            continue; /* the screen as it was composed: nothing it is made of changed */
        }
        in[0] = desc[e];
        in[1] = memory[b ? BBG : ABG];
        in[2] = memory[b ? BOBJ : AOBJ];
        in[3] = memory[b ? BBGEXT : ABGEXT];
        in[4] = memory[b ? BOBJEXT : AOBJEXT];
        in[5] = memory[VRAM];
        in[6] = layer3d;
        in[7] = khdays_gpu_hd_buffer();
        if (!gpu_skip("layers")) {
            /* each layer's colour at each DS pixel, looked up once for the N x N output pixels */
            Layers2dParams lp;
            SDL_GPUStorageTextureReadWriteBinding lio[1];
            SDL_GPUComputePass *lpass;
            SDL_zero(lp);
            lp.rows = KHDAYS_FRAME_H;
            SDL_zeroa(lio);
            lio[0].texture = layers1x[e];
            lio[0].cycle = true;
            lpass = SDL_BeginGPUComputePass(cmd, lio, 1, NULL, 0);
            SDL_BindGPUComputePipeline(lpass, layers_pipeline);
            SDL_BindGPUComputeStorageBuffers(lpass, 0, in, 8);
            SDL_PushGPUComputeUniformData(cmd, 0, &lp, sizeof(lp));
            SDL_DispatchGPUCompute(lpass, KHDAYS_FRAME_W / 8, KHDAYS_FRAME_H / 8, 1);
            SDL_EndGPUComputePass(lpass);
            gpu_split(device, &cmd, e == 0 ? "layers A" : "layers B");
            cp.layers1x_rows = KHDAYS_FRAME_H;
        }
        SDL_zeroa(io);
        io[0].texture = e == 0 && raster ? color3d : dummy3d[0]; /* read only: the 3D pass's output, not cycled */
        io[1].texture = e == 0 && raster ? attr3d : dummy3d[1];
        io[2].texture = screen[e];
        io[2].cycle = true;
        io[3].texture = e == 0 && hud ? hud_color : dummy_hud[0]; /* read only */
        io[4].texture = e == 0 && hud ? hud_attr : dummy_hud[1];
        io[5].texture = layers1x[e]; /* read only */
        io[6].texture = pre2d[e];
        /* A weaker card's one-screen sources, and its screens without the 3D in them (a menu's two screens)
         * (khdays_gfx_render_set_coarse_2d): a thread a DS pixel, its colour
         * given to its N x N output pixels, so the card composes a quarter of the pixels at 2x. The DS's own
         * graphics are the same at all of them; what is finer than a DS pixel is an element drawn finer than its
         * rectangle (the port's own text), whose tiles are composed at each output pixel (fine_parts), and the
         * 3D's edge where it crosses a HUD piece, which is lost. The Tab A9+'s field (Adreno 619, 2x): 47 ms a
         * picture composed at each output pixel, 22 ms so. Not a frame with a mod's finer tiles (finer_than_ds),
         * nor a screen kept whole (the capture, KHDAYS_GFX=check). KHDAYS_GPU_SKIP=coarse composes each output
         * pixel, to measure. Decided before the DS-sized pass below, which it replaces: that pass still starts a
         * thread at every output pixel, and from 3x up it was always the one taken -- the setting did nothing
         * there (the Thor at 5x: the same 42 pictures a second with it on or off). */
        static FineParts fine;
        int coarse = coarse_now() && (one != NULL || cp.source3d == 0u) && scale > 1 &&
                     !(e == 0 && (whole_a || p->capture)) && !checked_whole() && !finer_than_ds(p) &&
                     !gpu_skip("coarse");
        if (coarse) {
            fine_parts(p, e, one != NULL && !(e == 0 && whole_a), parts, part_count, screen_columns[e], &fine);
            coarse = fine.count[0] >= 0;
        }
        coarse_pictures[e] += coarse;
        if (!coarse && scale >= PRE_SCALE_MIN && cp.source3d == 0u && screen_columns[e] <= PRE_COLUMNS &&
            !gpu_skip("pre")) {
            /* each DS pixel once: whether its N x N output pixels are all alike. Not for an engine that reads the
             * 3D: its pixels are seldom alike, and the pass cost the TCL's Mali 2.5 ms for nothing (2026-11-05) */
            io[6].cycle = true;
            cp.pre_mode = 1u;
            pass = SDL_BeginGPUComputePass(cmd, io, 7, NULL, 0);
            SDL_BindGPUComputePipeline(pass, compose_engine[e]);
            SDL_BindGPUComputeStorageBuffers(pass, 0, in, 8);
            for (int k = 0; k < part_count; ++k) {
                cp.origin_x = (uint32_t)parts[k][0];
                cp.origin_y = (uint32_t)parts[k][1];
                compose_params(cmd, &cp);
                SDL_DispatchGPUCompute(pass, (uint32_t)(parts[k][2] - parts[k][0] + 7) / 8,
                                       (uint32_t)(parts[k][3] - parts[k][1] + 7) / 8, 1);
            }
            SDL_EndGPUComputePass(pass);
            gpu_split(device, &cmd, e == 0 ? "pre A" : "pre B");
            io[2].cycle = false; /* the screen the pass above bound: not made anew for the composition */
            io[6].cycle = false;
            cp.pre_mode = 2u;
        }
        pass = SDL_BeginGPUComputePass(cmd, io, 7, NULL, 0);
        SDL_BindGPUComputePipeline(pass, compose_engine[e]);
        SDL_BindGPUComputeStorageBuffers(pass, 0, in, 8);
        for (int set = coarse ? 0 : 1; set < 2 && !gpu_skip(e == 0 ? "2da" : "2db"); ++set) {
            /* each part in the same pass: the screen made anew (cycled) once, at its start; no texel twice */
            int (*const rects)[4] = coarse ? fine.rect[set] : parts;
            const int count = coarse ? fine.count[set] : part_count;
            const uint32_t per = coarse && set == 0 ? 1u : cp.scale;
            const uint32_t pre_mode = cp.pre_mode;
            if (coarse && set == 0) {
                cp.pre_mode = 3u;
            }
            for (int k = 0; k < count; ++k) {
                cp.origin_x = (uint32_t)rects[k][0];
                cp.origin_y = (uint32_t)rects[k][1];
                compose_params(cmd, &cp);
                SDL_DispatchGPUCompute(pass, ((uint32_t)(rects[k][2] - rects[k][0]) * per + 7) / 8,
                                       ((uint32_t)(rects[k][3] - rects[k][1]) * per + 7) / 8,
                                       1); /* the shader skips pixels past the edge */
            }
            cp.pre_mode = pre_mode;
        }
        SDL_EndGPUComputePass(pass);
        gpu_split(device, &cmd, e == 0 ? "2d A" : "2d B");
    }
    rows_drawn = 0;
    if (rows_count > 0 && p->upper == 0 && one == NULL) {
        /* the lower screen's (engine B's) rows asked for, apart from the DS's own */
        Compose2dParams cp;
        SDL_GPUStorageTextureReadWriteBinding io[7];
        SDL_GPUBuffer *in[8];
        SDL_GPUComputePass *pass;
        if (rows_texture == NULL || rows_scale != scale) {
            if (rows_texture != NULL) {
                SDL_ReleaseGPUTexture(device, rows_texture);
            }
            rows_texture = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                        SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                                        KHDAYS_FRAME_W * scale, KHDAYS_ROWS_MAX * scale);
            rows_scale = scale;
        }
        if (rows_texture != NULL) {
            SDL_zero(cp);
            cp.scale = (uint32_t)scale;
            cp.width = (uint32_t)(KHDAYS_FRAME_W * scale);
            cp.height = (uint32_t)(rows_count * scale);
            cp.row0 = (uint32_t)rows_row0;
            cp.layers_off = rows_off;
            in[0] = desc[1];
            in[1] = memory[BBG];
            in[2] = memory[BOBJ];
            in[3] = memory[BBGEXT];
            in[4] = memory[BOBJEXT];
            in[5] = memory[VRAM];
            in[6] = layer3d;
            in[7] = khdays_gpu_hd_buffer();
            SDL_zeroa(io);
            io[0].texture = dummy3d[0];
            io[1].texture = dummy3d[1];
            io[2].texture = rows_texture;
            io[2].cycle = true;
            io[3].texture = dummy_hud[0];
            io[4].texture = dummy_hud[1];
            io[5].texture = layers1x[1]; /* (bound for the pipeline; no rows of it are read: layers1x_rows 0) */
            io[6].texture = pre2d[1];   /* (bound for the pipeline; not read: pre_mode 0) */
            pass = SDL_BeginGPUComputePass(cmd, io, 7, NULL, 0);
            SDL_BindGPUComputePipeline(pass, compose_engine[1]);
            SDL_BindGPUComputeStorageBuffers(pass, 0, in, 8);
            compose_params(cmd, &cp);
            if (!gpu_skip("rows"))
                SDL_DispatchGPUCompute(pass, (cp.width + 7) / 8, (cp.height + 7) / 8, 1);
            SDL_EndGPUComputePass(pass);
            gpu_split(device, &cmd, "rows");
            rows_drawn = 1;
        }
    }
    if (shop_count != 0) {
        /* the Moogle Shop's screens wanted, each from its own description (no 3D); those not wanted now are
         * none until wanted again */
        if (++shop_pictures == 0) {
            shop_pictures = 1;
        }
        SDL_zeroa(shop_made);
    }
    for (int i = 0; i < shop_count; ++i) {
        const int s = shop_s[i], v = shop_v[i], b = engine_on(p, s) == 1;
        Compose2dParams cp;
        SDL_GPUStorageTextureReadWriteBinding io[7];
        SDL_GPUBuffer *in[8];
        SDL_GPUComputePass *pass;
        SDL_zero(cp);
        cp.scale = (uint32_t)shop_scale;
        cp.width = (uint32_t)(KHDAYS_FRAME_W * shop_scale);
        cp.height = (uint32_t)(KHDAYS_FRAME_H * shop_scale);
        cp.scale3d = cp.scale; /* no 3D drawn on the card in them */
        cp.width3d = cp.width;
        cp.height3d = cp.height;
        /* its 3D at 1x, as two screens have it without the 3D drawn (engine A's) */
        cp.source3d = v == KHDAYS_SHOP_NORMAL && one->shop_with3d[s] && !b && has3d ? 1u : 0u;
        in[0] = shop_desc[s][v];
        in[1] = memory[b ? BBG : ABG];
        in[2] = memory[b ? BOBJ : AOBJ];
        in[3] = memory[b ? BBGEXT : ABGEXT];
        in[4] = memory[b ? BOBJEXT : AOBJEXT];
        in[5] = memory[VRAM];
        in[6] = layer3d;
        in[7] = khdays_gpu_hd_buffer();
        SDL_zeroa(io);
        io[0].texture = dummy3d[0];
        io[1].texture = dummy3d[1];
        io[2].texture = shop_screen[s][v];
        io[2].cycle = true;
        io[3].texture = dummy_hud[0];
        io[4].texture = dummy_hud[1];
        io[5].texture = layers1x[b]; /* (bound for the pipeline; no rows of it are read: layers1x_rows 0) */
        io[6].texture = pre2d[b];   /* (bound for the pipeline; not read: pre_mode 0) */
        pass = SDL_BeginGPUComputePass(cmd, io, 7, NULL, 0);
        SDL_BindGPUComputePipeline(pass, compose_for(0, 0)); /* a shop's description: not split, not wide */
        SDL_BindGPUComputeStorageBuffers(pass, 0, in, 8);
        compose_params(cmd, &cp);
        SDL_DispatchGPUCompute(pass, (cp.width + 7) / 8, (cp.height + 7) / 8, 1);
        SDL_EndGPUComputePass(pass);
        shop_made[s][v] = shop_pictures;
    }
    if (one != NULL) {
        /* the one-screen picture, from both screens and the 3D (onescreen.hlsl) */
        OneScreenParams op;
        SDL_GPUStorageTextureReadWriteBinding io[8];
        SDL_GPUBuffer *in[4];
        SDL_GPUTexture *backdrop = backdrop_on_card(cmd, one);
        SDL_GPUComputePass *pass;
        const int upper = p->upper; /* the engine on the upper screen */
        const uint32_t *desc_upper = p->desc2d[upper], *desc_lower = p->desc2d[!upper];
        const uint32_t bd = one->frame == (int)KOS_UPPER ? desc_upper[KD_BACKDROP] : desc_lower[KD_BACKDROP];
        SDL_zero(op);
        op.scale = (uint32_t)scale;
        op.width = (uint32_t)(one->columns * scale);
        op.height = (uint32_t)(KHDAYS_FRAME_H * scale);
        op.has3d = (uint32_t)(raster != 0);
        op.scale3d = raster ? (uint32_t)scale3d : (uint32_t)scale;
        op.width3d = raster ? (uint32_t)(one->columns * scale3d) : op.width;
        op.height3d = raster ? (uint32_t)(KHDAYS_FRAME_H * scale3d) : op.height;
        op.smooth3d = (uint32_t)smooth3d;
        op.hud3d = (uint32_t)hud;
        if (raster) {
            const uint8_t *io3d = p->io3d;
            op.disp3dcnt = p->disp3dcnt;
            op.swap_params = p->list3d.swap_params;
            op.fine = (uint32_t)khdays_raster3d_fine();
            op.clear_color = io32(io3d, 0x50);
            op.clear_depth = io32(io3d, 0x54);
            op.fog_color = io32(io3d, 0x58);
            op.fog_offset = io32(io3d, 0x5c);
            for (int i = 0; i < 4; ++i) {
                op.edge_colors[i] = io32(io3d, 0x30 + 4u * (unsigned)i);
            }
            for (int i = 0; i < 8; ++i) {
                op.fog_table[i] = io32(io3d, 0x60 + 4u * (unsigned)i);
            }
        }
        op.shape_count = (uint32_t)one->shape_count;
        op.frame = (uint32_t)one->frame;
        op.rule = (uint32_t)one->rule;
        op.masters = (desc_upper[KD_MASTER] & 0xffffu) | (desc_lower[KD_MASTER] & 0xffffu) << 16;
        {
            /* the backdrop colour (BGR555) widened to 8-bit channels, as kd_fine does */
            const uint32_t r = bd & 31u, g = (bd >> 5) & 31u, b = (bd >> 10) & 31u;
            op.backdrop = ((r << 3) | (r >> 2)) | ((g << 3) | (g >> 2)) << 8 | ((b << 3) | (b >> 2)) << 16;
        }
        op.a_upper = upper == 0 ? 1u : 0u;
        op.hide3d = one->hide3d_all ? 1u : 0u;
        if (backdrop != no_backdrop) {
            /* covering the picture (or inside it: backdrop_fit), its aspect kept, held at its
             * anchor; an animated picture's pieces on its ground, from the atlas */
            const int pw = one->picture_w > 0 ? one->picture_w : one->backdrop_w;
            const int ph = one->picture_h > 0 ? one->picture_h : one->backdrop_h;
            const float sw = (float)op.width / (float)pw, sh = (float)op.height / (float)ph;
            const float s = (sw > sh) != (one->backdrop_fit != 0) ? sw : sh;
            op.backdrop_w = (uint32_t)one->backdrop_w;
            op.backdrop_h = (uint32_t)one->backdrop_h;
            op.backdrop_scale = s;
            op.backdrop_x = ((float)op.width - (float)pw * s) * one->backdrop_ax;
            op.backdrop_y = ((float)op.height - (float)ph * s) * one->backdrop_ay;
            if (one->animated) {
                op.animated = 1u;
                op.piece_count = (uint32_t)one->piece_count;
                op.backdrop = one->ground & 0xffffffu;
                if (binned) {
                    op.bin_tile = BIN_TILE;
                    op.bin_tiles_x = bin_tiles_x;
                    op.bin_tiles_y = bin_tiles_y;
                }
            }
        }
        in[0] = shape_buffer;
        in[1] = key_buffer;
        in[2] = piece_buffer;
        in[3] = khdays_gpu_hd_buffer();
        SDL_zeroa(io);
        io[0].texture = raster ? color3d : dummy3d[0];
        io[1].texture = raster ? attr3d : dummy3d[1];
        io[2].texture = screen[upper];
        io[3].texture = screen[!upper];
        io[4].texture = picture;
        io[4].cycle = true;
        io[5].texture = backdrop; /* read only */
        io[6].texture = hud ? hud_color : dummy_hud[0];
        io[7].texture = hud ? hud_attr : dummy_hud[1];
        pass = SDL_BeginGPUComputePass(cmd, io, 8, NULL, 0);
        if (!gpu_skip("one")) {
            /* the texels a shape can reach, then the rest without the shapes (one_covers): no texel twice.
             * KHDAYS_GPU_SKIP=tiles draws the picture in one piece, to measure */
            static OneCovers covers;
            const uint32_t shape_count = op.shape_count;
            one_covers(one, scale, (int)op.width, (int)op.height, &covers);
            if (covers.count[0] < 0 || !partial_2d || reference || gpu_skip("tiles")) {
                covers.count[0] = 1;
                covers.count[1] = 0;
                covers.rect[0][0][0] = covers.rect[0][0][1] = 0;
                covers.rect[0][0][2] = (int)op.width;
                covers.rect[0][0][3] = (int)op.height;
            }
            for (int set = 0; set < 2; ++set) {
                if (covers.count[set] == 0) {
                    continue;
                }
                SDL_BindGPUComputePipeline(pass, set == 1 && onescreen_plain != NULL ? onescreen_plain : onescreen);
                SDL_BindGPUComputeStorageBuffers(pass, 0, in, 4);
                op.shape_count = set == 1 ? 0u : shape_count;
                for (int k = 0; k < covers.count[set]; ++k) {
                    const int *r = covers.rect[set][k];
                    op.origin_x = (uint32_t)r[0];
                    op.origin_y = (uint32_t)r[1];
                    SDL_PushGPUComputeUniformData(cmd, 0, &op, sizeof(op));
                    SDL_DispatchGPUCompute(pass, (uint32_t)(r[2] - r[0] + 7) / 8, (uint32_t)(r[3] - r[1] + 7) / 8, 1);
                }
            }
        }
        SDL_EndGPUComputePass(pass);
        gpu_split(device, &cmd, "one");
    }
    if (blend) {
        if (one != NULL) {
            take_capture(cmd, p, &plan, picture, picture_w, picture_h);
        } else {
            take_capture(cmd, p, &plan, screen[0], screen_columns[0] * scale, KHDAYS_FRAME_H * scale);
        }
    }
    /* what is shown, shrunk to the size shown (the picture on one screen, else the screens) */
    picture_shrunk = screens_shrunk = 0;
    if (shown > 0.0f && shrinker != NULL) {
        if (one != NULL) {
            picture_shrunk = shrink(cmd, picture, picture_w, picture_h, picture_columns, &shrunk_picture);
        } else {
            screens_shrunk = 1;
            for (int e = 0; e < 2; ++e) {
                screens_shrunk = screens_shrunk && shrink(cmd, screen[e], screen_columns[e] * scale,
                                                          KHDAYS_FRAME_H * scale, screen_columns[e], &shrunk_screen[e]);
            }
        }
    }
    if (!gpu_submit_timed(device, cmd, "2d")) {
        valid = 0;
        return 0;
    }
    return 1;
}

int khdays_gfx_render_draw(const KhdaysFramePackage *p, int scale, int scale3d, int raster)
{
    return draw(p, scale, scale3d, raster, NULL);
}

int khdays_gfx_render_draw_one(const KhdaysFramePackage *p, int scale, int scale3d, int raster,
                               const KhdaysOneScreenDraw *one)
{
    return draw(p, scale, scale3d, raster, one);
}

void khdays_gfx_render_set_smooth_3d(int on)
{
    smooth3d = on != 0;
}

SDL_Texture *khdays_gfx_render_picture(SDL_Renderer *renderer, int *columns)
{
    *columns = picture_columns;
    if (picture == NULL) {
        return NULL;
    }
    if (picture_shrunk) {
        return wrap(renderer, shrunk_picture.texture, shrunk_picture.w, shrunk_picture.h, &shrunk_picture.wrapped,
                    &shrunk_picture.wrapped_for);
    }
    return wrap(renderer, picture, picture_w, picture_h, &picture_wrapped, &picture_for);
}

SDL_Texture *khdays_gfx_render_texture(SDL_Renderer *renderer, int engine)
{
    if (screen[engine] == NULL) {
        return NULL;
    }
    if (screens_shrunk) {
        Shrunk *s = &shrunk_screen[engine];
        return wrap(renderer, s->texture, s->w, s->h, &s->wrapped, &s->wrapped_for);
    }
    return wrap(renderer, screen[engine], screen_columns[engine] * screen_scale, KHDAYS_FRAME_H * screen_scale,
                &wrapped[engine], &wrapped_for[engine]);
}

void khdays_gfx_render_set_lower_rows(int row0, int rows, uint32_t layers_off)
{
    rows_row0 = row0;
    rows_count = rows < 0 ? 0 : rows > KHDAYS_ROWS_MAX ? KHDAYS_ROWS_MAX : rows;
    rows_off = layers_off;
}

SDL_GPUTexture *khdays_gfx_render_lower_rows(int *row0, int *rows, int *scale)
{
    if (!rows_drawn || rows_texture == NULL) {
        return NULL;
    }
    *row0 = rows_row0;
    *rows = rows_count;
    *scale = rows_scale;
    return rows_texture;
}

SDL_Texture *khdays_gfx_render_shop(SDL_Renderer *renderer, int lower, int variant, int *scale, unsigned *made)
{
    if (lower < 0 || lower > 1 || variant < 0 || variant >= KHDAYS_SHOP_VARIANTS ||
        shop_screen[lower][variant] == NULL || shop_made[lower][variant] == 0) {
        return NULL;
    }
    *scale = shop_scale;
    *made = shop_made[lower][variant];
    return wrap(renderer, shop_screen[lower][variant], KHDAYS_FRAME_W * shop_scale, KHDAYS_FRAME_H * shop_scale,
                &shop_wrapped[lower][variant], &shop_wrapped_for[lower][variant]);
}

int khdays_gfx_render_shop_made(int lower, int variant)
{
    return lower >= 0 && lower <= 1 && variant >= 0 && variant < KHDAYS_SHOP_VARIANTS &&
           shop_screen[lower][variant] != NULL && shop_made[lower][variant] != 0;
}

int khdays_gfx_render_columns(int engine)
{
    return screen_columns[engine];
}

int khdays_gfx_render_captured_shown(void)
{
    /* engine B is on the screen engine A was on in the previous package (the upper when A is on the lower) */
    return capture_copy_shown() != NULL;
}

SDL_Texture *khdays_gfx_render_captured(SDL_Renderer *renderer, int *columns)
{
    CaptureCopy *k = capture_copy_shown();
    if (k == NULL) {
        return NULL;
    }
    *columns = k->columns;
    return wrap(renderer, k->texture, k->w, k->h, &k->wrapped, &k->wrapped_for);
}

uint32_t *khdays_gfx_render_read(int engine, int *width, int *height)
{
    /* as shown: shrunk to the size shown */
    SDL_GPUTexture *texture;
    int w, h;
    if (engine == 3) {
        const CaptureCopy *k = capture_copy_shown();
        if (k == NULL) {
            return NULL;
        }
        texture = k->texture;
        w = k->w;
        h = k->h;
    } else if (engine == 2) {
        texture = picture_shrunk ? shrunk_picture.texture : picture;
        w = picture_shrunk ? shrunk_picture.w : picture_w;
        h = picture_shrunk ? shrunk_picture.h : picture_h;
    } else {
        const int e = engine == 1 ? 1 : 0;
        texture = screens_shrunk ? shrunk_screen[e].texture : screen[e];
        w = screens_shrunk ? shrunk_screen[e].w : screen_columns[e] * screen_scale;
        h = screens_shrunk ? shrunk_screen[e].h : KHDAYS_FRAME_H * screen_scale;
    }
    const Uint32 bytes = (Uint32)(w * h * 4);
    SDL_GPUTransferBufferCreateInfo info;
    SDL_GPUTransferBuffer *download;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUFence *fence;
    uint32_t *pixels = NULL;
    if (texture == NULL) {
        SDL_SetError("nothing drawn yet");
        return NULL;
    }
    SDL_zero(info);
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = bytes;
    download = SDL_CreateGPUTransferBuffer(device, &info);
    cmd = download != NULL ? SDL_AcquireGPUCommandBuffer(device) : NULL;
    if (cmd == NULL) {
        if (download != NULL)
            SDL_ReleaseGPUTransferBuffer(device, download);
        return NULL;
    }
    {
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region;
        SDL_GPUTextureTransferInfo to;
        SDL_zero(region);
        SDL_zero(to);
        region.texture = texture;
        region.w = (Uint32)w;
        region.h = (Uint32)h;
        region.d = 1;
        to.transfer_buffer = download;
        SDL_DownloadFromGPUTexture(copy, &region, &to);
        SDL_EndGPUCopyPass(copy);
    }
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence != NULL && SDL_WaitForGPUFences(device, true, &fence, 1)) {
        const uint8_t *rgba = (const uint8_t *)SDL_MapGPUTransferBuffer(device, download, false);
        pixels = rgba != NULL ? (uint32_t *)SDL_malloc(bytes) : NULL;
        if (pixels != NULL) {
            for (int i = 0; i < w * h; ++i) {
                pixels[i] = (uint32_t)rgba[4 * i] << 16 | (uint32_t)rgba[4 * i + 1] << 8 | rgba[4 * i + 2];
            }
        }
        if (rgba != NULL) {
            SDL_UnmapGPUTransferBuffer(device, download);
        }
    }
    if (fence != NULL) {
        SDL_ReleaseGPUFence(device, fence);
    }
    SDL_ReleaseGPUTransferBuffer(device, download);
    *width = w;
    *height = h;
    return pixels;
}
