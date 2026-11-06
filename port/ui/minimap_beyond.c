/* minimap_beyond.h. The game plots the map's icons (enemies, chests, exits, the goal, the party) into a page of 120 x 80
 * pixels, its BG2 tiles behind the panel's hole, and drops whatever falls outside it (Ov002_PlotPagePair). The map
 * itself is the whole room in BG3, scrolled to the player, so a taller box shows real map above and below the DS's; the
 * pixels the game dropped there are kept here and drawn as BG3's, in the page's own palette, so they are composed,
 * blended and dimmed as the map is. The page's own pixels stay the game's: an icon across the box's edge is drawn once,
 * its two parts meeting there. The page reaches VRAM through the game's command queue, so each redraw is kept with the
 * page it made, and the one shown is the one whose page VRAM holds: the two parts never a frame apart. */
#include "minimap_beyond.h"
#include "bg.h"
#include "elements.h"
#include "parts.h"

#include <string.h>
#include <windows.h> /* Interlocked* (the POSIX build's shim) */

#define PAGE_W 120 /* the page, DS pixels */
#define PAGE_H 80
#define BOX_X 104 /* where the panel shows it */
#define BOX_Y 56
#define BEYOND 48             /* rows kept above the page and below it */
#define PAGE_FIRST_TILE 0x139 /* the page's first tile, at the box's top left: the panel is up */
#define MAP_ENGINE 1
#define PAGE_BG 2
#define MAP_BG 3

/* A redraw: the page as the game plotted it, and what it dropped: [0] above the page, its last row the one over the
 * page's first; [1] below; a pixel's colour index, 0 none. */
typedef struct Redraw {
    int x, y; /* the page's place on the map (the scene's nX, nY): BG3's scroll goes with it */
    unsigned char page[PAGE_H][PAGE_W];
    unsigned char bands[2][BEYOND][PAGE_W];
} Redraw;
#define REDRAWS 4 /* the last ones, the newest at `newest` */
static Redraw drawing, redraws[REDRAWS];
static int redraw_count, newest;
static unsigned char kept[2][BEYOND][PAGE_W]; /* the bands drawn now */
static int recording, have, images[2] = {-1, -1}, image_palette[2] = {-1, -1}, image_rows[2];
static int written[2]; /* the image holds kept[band] as it is */
/* the field's rows the host composes past the box, from the render thread, in one word: a count of the frames that
 * said so << 24 | row0 << 8 | rows (rows 0: none); a request the host stops making is gone after STALE_EMITS of the
 * game's */
static volatile LONG field_request;
static LONG serial_seen;
static int emits_since;
#define STALE_EMITS 30

void khdays_minimap_beyond_begin(void)
{
    memset(&drawing, 0, sizeof(drawing));
    recording = 1;
}

static void keep(int x, int y, int index)
{
    if (index == 0 || x < 0 || x >= PAGE_W) {
        return;
    }
    if (y < 0 && y >= -BEYOND) {
        drawing.bands[0][BEYOND + y][x] = (unsigned char)index;
    } else if (y >= PAGE_H && y < PAGE_H + BEYOND) {
        drawing.bands[1][y - PAGE_H][x] = (unsigned char)index;
    } else if (y >= 0 && y < PAGE_H) {
        drawing.page[y][x] = (unsigned char)index; /* as the game writes it into the page */
    }
}

void khdays_minimap_beyond_pair(int x, int y, int value)
{
    if (!recording) {
        return;
    }
    keep(x, y, value & 0xf); /* the low nibble at x, the high one after it */
    keep(x + 1, y, (value >> 4) & 0xf);
}

void khdays_minimap_beyond_end(int x, int y)
{
    if (!recording) {
        return;
    }
    recording = 0;
    newest = (newest + 1) % REDRAWS;
    drawing.x = x;
    drawing.y = y;
    redraws[newest] = drawing;
    redraw_count += redraw_count < REDRAWS;
    have = 1;
    khdays_ui_changed(); /* the next VBlank draws again, though only the bands changed */
}

void khdays_minimap_beyond_reset(void)
{
    recording = have = redraw_count = 0;
    memset(kept, 0, sizeof(kept));
    written[0] = written[1] = 0;
}

void khdays_minimap_beyond_lower_rows(int row0, int rows, unsigned layers_off)
{
    /* the field's request leaves the panel (BG2) out and reaches over the box; a list's is under the screen */
    const int field = rows > 0 && row0 >= 0 && row0 < BOX_Y && row0 + rows > BOX_Y + PAGE_H && (layers_off >> 10 & 1u);
    const LONG before = InterlockedCompareExchange(&field_request, 0, 0);
    const LONG serial = (LONG)(((unsigned long)before >> 24) + 1u) & 0xff;
    InterlockedExchange(&field_request, (LONG)((unsigned long)serial << 24 |
                                               (field ? (unsigned long)row0 << 8 | (unsigned long)rows : 0u)));
}

/* The page's palette while the panel shows it, from the page's first cell in BG2's map, and where its tiles are in
 * the engine's BG memory; -1 when the panel is not up. */
static int page_palette(uint32_t *tiles)
{
    KhdaysBgLayer l;
    uint32_t entry;
    if (!khdays_bg_text_layer(MAP_ENGINE, PAGE_BG, &l)) {
        return -1;
    }
    entry = khdays_bg_map_entry(MAP_ENGINE, &l, ((BOX_X + l.hofs) & 511) / 8, ((BOX_Y + l.vofs) & 511) / 8);
    *tiles = l.chars + PAGE_FIRST_TILE * 32u;
    return (entry & 0x3ffu) == PAGE_FIRST_TILE ? (int)(entry >> 12) : -1;
}

/* The redraw whose page VRAM holds now (15 x 10 tiles of 4 bits); -1 when VRAM holds none recorded, or when two
 * recorded with that page differ in their place or their bands (which of them is shown is not known); -2 when no
 * memory is mapped there. */
static int shown_redraw(uint32_t tiles)
{
    static unsigned char shown[PAGE_H][PAGE_W];
    const uint8_t *first = khdays_bg_memory(MAP_ENGINE, tiles), *last = khdays_bg_memory(MAP_ENGINE, tiles + 0x12bfu);
    int pick = -1;
    if (first == NULL || last != first + 0x12bf) {
        return -2;
    }
    {
        for (int y = 0; y < PAGE_H; ++y) {
            for (int x = 0; x < PAGE_W; ++x) {
                const uint8_t pair = first[((y / 8) * (PAGE_W / 8) + x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2];
                shown[y][x] = (unsigned char)(x & 1 ? pair >> 4 : pair & 0xf);
            }
        }
        for (int n = 0; n < redraw_count; ++n) {
            const int r = (newest - n + REDRAWS) % REDRAWS;
            if (memcmp(redraws[r].page, shown, sizeof(shown)) != 0) {
                continue;
            }
            if (pick < 0) {
                pick = r;
            } else if (redraws[r].x != redraws[pick].x || redraws[r].y != redraws[pick].y ||
                       memcmp(redraws[r].bands, redraws[pick].bands, sizeof(redraws[r].bands)) != 0) {
                return -1;
            }
        }
    }
    return pick;
}

/* The bands of the redraw whose page VRAM holds now; the ones shown kept while that is not known. */
static void choose(uint32_t tiles)
{
    int pick = shown_redraw(tiles);
    if (pick == -1) {
        return;
    }
    pick = pick >= 0 ? pick : newest; /* nothing there to agree with */
    for (int band = 0; band < 2; ++band) {
        if (memcmp(kept[band], redraws[pick].bands[band], sizeof(kept[band])) != 0) {
            memcpy(kept[band], redraws[pick].bands[band], sizeof(kept[band]));
            written[band] = 0;
        }
    }
}

static int any(int band, int from, int to)
{
    for (int y = from; y < to; ++y) {
        for (int x = 0; x < PAGE_W; ++x) {
            if (kept[band][y][x] != 0) {
                return 1;
            }
        }
    }
    return 0;
}

int khdays_minimap_beyond_scroll(uint32_t *bg3ofs, int *redraws_back)
{
    uint32_t tiles = 0;
    int r;
    if (!have || page_palette(&tiles) < 0 || (r = shown_redraw(tiles)) < 0) {
        return 0;
    }
    *bg3ofs = ((uint32_t)(redraws[r].x - 0x68) & 0x1ffu) | (((uint32_t)(redraws[r].y - 0x38) << 16) & 0x1ff0000u);
    *redraws_back = (newest - r + REDRAWS) % REDRAWS;
    return 1;
}

void khdays_minimap_beyond_emit(void)
{
    static const char *const ids[2] = {"hud.map_above", "hud.map_below"};
    const unsigned long request = (unsigned long)InterlockedCompareExchange(&field_request, 0, 0);
    const LONG serial = (LONG)(request >> 24);
    const int row0 = (int)(request >> 8 & 0xffu), rows = (int)(request & 0xffu);
    /* the rows the lower screen shows over the box and under it, at most BEYOND each */
    int shown[2];
    int palette;
    uint32_t tiles = 0;
    if (serial != serial_seen) {
        serial_seen = serial;
        emits_since = 0;
    } else if (emits_since < STALE_EMITS) {
        ++emits_since;
    }
    shown[0] = rows > 0 ? BOX_Y - row0 : 0;
    shown[1] = rows > 0 ? row0 + rows - (BOX_Y + PAGE_H) : 0;
    if (!have || emits_since >= STALE_EMITS || (shown[0] <= 0 && shown[1] <= 0) ||
        (palette = page_palette(&tiles)) < 0) {
        return;
    }
    choose(tiles);
    for (int band = 0; band < 2; ++band) {
        const int n = shown[band] < 0 ? 0 : shown[band] > BEYOND ? BEYOND : shown[band];
        const int from = band == 0 ? BEYOND - n : 0, to = band == 0 ? BEYOND : n;
        KhdaysUiElement e;
        if (n == 0 || !any(band, from, to)) {
            continue;
        }
        if (images[band] < 0 || image_rows[band] != n) {
            /* as many rows as the lower screen shows of it: nothing of it lies over the pieces beyond them */
            if (images[band] >= 0) {
                khdays_ui_image_put(images[band]);
            }
            images[band] = khdays_ui_image_get(PAGE_W, n);
            image_rows[band] = n;
            written[band] = 0;
            if (images[band] < 0) {
                continue;
            }
        }
        if (!written[band] || image_palette[band] != palette) {
            uint32_t *texels = khdays_ui_image_begin(images[band]);
            if (texels != NULL) {
                for (int y = 0; y < n; ++y) {
                    for (int x = 0; x < PAGE_W; ++x) {
                        const unsigned index = kept[band][from + y][x];
                        texels[y * PAGE_W + x] = index != 0 ? KET_OPAQUE | ((unsigned)palette * 16u + index) : 0u;
                    }
                }
                khdays_ui_image_end(images[band]);
                written[band] = 1;
                image_palette[band] = palette;
            }
        }
        memset(&e, 0, sizeof(e));
        e.id = ids[band];
        e.screen = MAP_ENGINE;
        e.slot = MAP_BG; /* the map's own layer: under the panel, over nothing but the map */
        e.x = (float)BOX_X;
        e.y = (float)(band == 0 ? BOX_Y - n : BOX_Y + PAGE_H);
        e.w = (float)PAGE_W;
        e.h = (float)n;
        e.image = images[band];
        khdays_ui_emit_fixed(KHDAYS_UI_HUD, &e); /* where the map is: BG3, which the layout never moves */
    }
}
