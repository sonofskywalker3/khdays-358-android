/* The 2D composed once a DS dot (gfx_render.c `sparse`): compose2d.hlsl's DS-sized pass gives each DS dot a
 * verdict; shaders/spread2d.hlsl writes the output texels of the dots alike at every place in them, and lists the
 * others, which compose2d.hlsl then composes at each of their output texels. The list lives in the verdicts'
 * texture (pre2d) below the verdicts' rows: its count at (0, SPREAD_LIST_ROW), entry i at texel i + 1 of those
 * rows (x | y << 16, the dot within the screen). Plain C, only #defines: the shaders include it too. */
#ifndef KHDAYS_GFX_SPREAD2D_H
#define KHDAYS_GFX_SPREAD2D_H

#define SPREAD_LIST_W 2048u  /* the verdicts' texture's width: 2 texels a dot, 1024 dots a row (PRE_COLUMNS) */
#define SPREAD_LIST_ROW 768u /* below the verdicts' 4 x 192 rows */
#define SPREAD_LIST_ROWS 97u /* 1 + 1024 x 192 entries, 2048 a row */
#define SPREAD_GROUPS_X 64u  /* the list's composition: groups of 64 threads, 64 groups a row of the dispatch */
/* below the list: a bit a dot (y x 1024 + x), set when the dot is put on the list by one of its texels (those that
 * spread2d.hlsl can't work out of a PRE_NOTHING_BACKDROP or PRE_SAME_ALPHA3D dot), emptied each picture */
#define SPREAD_FLAG_ROW (SPREAD_LIST_ROW + SPREAD_LIST_ROWS)
#define SPREAD_FLAG_WORDS 6144u /* 1024 x 192 bits */
#define SPREAD_FLAG_ROWS 3u

/* the verdicts compose2d.hlsl's DS-sized pass writes */
#define PRE_EACH 0u    /* composed at each output pixel */
#define PRE_SAME 1u    /* the same at every place in the DS pixel: its colour composed once */
#define PRE_3D_ONLY 2u /* only the 3D there (no layer over it, no sprite, no element): the 3D composed alone */
#define PRE_NOTHING 3u /* the one-screen picture's source with only the 3D there: none of the 2D shows */
/* ... but the backdrop is a blend's second target, so where the 3D is translucent it is blended over the backdrop
 * by its alpha (compose.h kd_split): spread2d.hlsl reads the 3D's alpha at each texel (SPREAD_SOLID) */
#define PRE_NOTHING_BACKDROP 4u
/* The one-screen picture's source where the 3D was read and nothing finer than the DS pixel: kd_split depends on the
 * 3D only through its alpha there (0 where moved, 31 beside its 256 columns), so a texel whose 3D has the alpha the
 * DS-sized pass composed with (PRE_A3_SHIFT) takes its colour; spread2d.hlsl reads it at each texel */
#define PRE_SAME_ALPHA3D 5u
#define PRE_A3_SHIFT 16u /* in the verdict word: that alpha, 5 bits */
/* A dot's verdict word: the verdict in bits 0-7, SPREAD_WHY_* (below) from bit 8 when it is not PRE_SAME,
 * PRE_SAME_ALPHA3D's alpha from bit 16. Its other word: the colour of a PRE_SAME or PRE_SAME_ALPHA3D dot, else a bit
 * for each of the first 31 elements that shows something in the dot (the others are not looked at there:
 * compose2d.hlsl KD_ELEMENT_SKIP). */
#define PRE_VERDICT 0xffu
#define PRE_WHY_SHIFT 8u
#define PRE_ELEMENTS 0x7fffffffu
#define PRE_ELEMENTS_MOST 31u

/* spread2d.hlsl's `flags` */
#define SPREAD_SPLIT 1u /* the description's KDF_SPLIT: the colour is kd_split's */
#define SPREAD_FINE 2u  /* KDF_FINE: 8-bit channels */
#define SPREAD_RESET 4u /* the list emptied, the dispatch's groups none, the dots' flags cleared */
#define SPREAD_STATS 8u /* the listed dots counted by why (below), at the list's last row's last texels */
#define SPREAD_SOLID 16u /* the card's 3D is bound (source3d 2): PRE_NOTHING_BACKDROP's and PRE_SAME_ALPHA3D's
                          * texels are worked out from its alpha at each (raster3d_at.hlsli), else listed */
#define SPREAD_ALL 32u /* every dot listed (KHDAYS_GPU_SKIP=spreadall: to find a fault) */

/* Why a dot is listed: compose2d.hlsl's DS-sized pass writes these above the verdict of a dot not PRE_SAME */
#define SPREAD_WHY_PLACE 1u    /* a texel of a replacement or an element decided its colour */
#define SPREAD_WHY_3D 2u       /* the 3D was read */
#define SPREAD_WHY_STACK3D 4u  /* only the 3D lay at the dot */
#define SPREAD_WHY_ONLY3D 8u   /* ... and the screen's flags let it be the 3D alone */
#define SPREAD_WHY_SPLIT 16u   /* the one-screen picture's source */
#define SPREAD_WHY_BACKDROP 32u /* the backdrop is a blend's second target */
#define SPREAD_WHY_WIDE 64u    /* widescreen */
#define SPREAD_WHYS 7u
#define SPREAD_STATS_X (SPREAD_LIST_W - 8u) /* the count of listed dots with why bit b at x + b; at x + 7 all */
#define SPREAD_STATS_ROW (SPREAD_LIST_ROW + SPREAD_LIST_ROWS - 1u)

#endif
