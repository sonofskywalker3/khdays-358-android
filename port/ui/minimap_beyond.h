/* The map's icons past its box on the field's INFORMATION panel (minimap_beyond.c), for a lower screen laid out taller
 * than the DS's (host/screens/lower_screens.c shows the map's own layer above and below the box). */
#ifndef KHDAYS_UI_MINIMAP_BEYOND_H
#define KHDAYS_UI_MINIMAP_BEYOND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game's redraw of the map's page: begun, each pair of pixels it plots (Ov002_PlotPagePair, before its clipping:
 * x and y in the page's 120 x 80), done; and the panel taken down. */
void khdays_minimap_beyond_begin(void);
void khdays_minimap_beyond_pair(int x, int y, int value);
void khdays_minimap_beyond_end(int x, int y); /* the page's place on the map, as the scene holds it after the redraw */
void khdays_minimap_beyond_reset(void);

/* The extra rows of the lower screen the host composes for the frame it presents (gfx_render.h
 * khdays_gfx_render_set_lower_rows): DS rows from `row0`, `rows` of them (0: none), `layers_off` left out. Said for each
 * frame the lower display is laid out; the icons go once it stops. */
void khdays_minimap_beyond_lower_rows(int row0, int rows, unsigned layers_off);

/* While the panel shows the page: the BG3 scroll that goes with the page VRAM holds (Ov002_StepPageScroll's), and how
 * many redraws ago that page was made; 0 when that page is not one recorded. */
int khdays_minimap_beyond_scroll(uint32_t *bg3ofs, int *redraws_back);

/* The HUD part's elements: the icons above and below the box, in the map's layer (BG3) and palette. */
void khdays_minimap_beyond_emit(void);

#ifdef __cplusplus
}
#endif

#endif
