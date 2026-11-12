// compose2d.hlsl's constant buffer and what `modes` packs (gfx_render.c Compose2dBlock); spread2d.hlsl reads it too.

// 32 words, 128 bytes, and no more: an Adreno 619 (driver 512.762.40) gave this shader nothing of its block past
// them. When it was 36 words, row0 and the three after it read as 0 there, or as what layers2d.hlsl's pass had
// left behind when that ran just before (the title's art in three bands, the one-screen picture without its
// menus); a block of 32 is read whole (measured 2026-11-10). So the small ones share a word (`modes`).
cbuffer Params : register(b0, space2) {
    uint scale;         // N
    uint width;         // the screen and raster3d's targets, pixels
    uint height;
    uint modes;         // source3d, smooth3d, hud3d, pre_mode and the origin (below)
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
#define pre_mode ((modes >> 4) & 3u)  // 0: each pixel composed here; 1: this is the DS-sized pass writing `pre`; 2: `pre` read;
                                      // 3: a thread a DS pixel, its colour given to its N x N output pixels
#define list_mode ((modes >> 6) & 1u) // 1 (with pre_mode 2): the output texels of the dots spread2d.hlsl listed, alone
