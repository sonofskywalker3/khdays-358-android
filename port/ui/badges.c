/* The badges' icons on the field's mission panel (badges.h): the DS shows a mission's badges only on
 * the camp's mission list; the port draws them left of the chest icon too, from the list's own pieces
 * (UI/cm/msl_<language>.msi.z, UI/cm/cm.p2 entries 17, 19, 20), lit from the save or a pickup. None in
 * Mission Mode. docs/RUNTIME_MAP.md section 14, "The badges on the mission's panel". */
#include "badges.h"
#include "bg.h"
#include "elements.h"
#include "mission_list.h"
#include "parts.h"
#include "../host/app/host.h"
#include "../runtime/display/frame_package.h"
#include "../runtime/core/window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the game's */
extern int GameState_GetField(int field, int bits);
extern int GetLanguage(void);

#define RANK_OPEN 0 /* the icons, characters 0x80-0x83: the ordeal badge's and the blazon's, */
#define RANK_FOUND 1
#define UNITY_OPEN 2 /* then the unity badge's */
#define UNITY_FOUND 3
#define FIRST_ICON 0x80

#define COUNTER_CHARACTER 0xd3
#define PANEL_EDGE 0x090         /* the mission panel's upper-left corner tile, then +1 and +2 along its edge */
#define COUNTER_COLUMN 28        /* the counter's first cell, counted from that corner */
#define COUNTER_PAGE 0x0207f634u /* the counter's state (Ov002_DrawPageCounter) */

#define MODE_FLAGS 0x0204c240u /* +2: the mission's room key */
#define MISSION 0x0204c23cu
#define FIELD_CONTEXT 0x0207fa00u
#define FOUND_FIELD 0x92b
#define FOUND_HERE 0x8d4e /* in the field's context: a byte every 4 */

static int in_game_ram(uint32_t p)
{
    return khdays_game_ram(p); /* main RAM or the game's heaps */
}

/* --- The icons ------------------------------------------------------------------------ */

static uint8_t icons[4][64];    /* each icon's 8 x 8 palette indices */
static const uint32_t *colours; /* the list's palette, as the picture's colours (RGBA) */
static int graphics;            /* 0: not read yet, 1: read, -1: not there */

/* The colour the composition makes of a DS colour (elements.c rgb_of). */
static uint32_t rgba_of(uint32_t bgr555)
{
    uint32_t out = 0xff000000u;
    for (int n = 0; n < 3; ++n) {
        const uint32_t c = (bgr555 >> (5 * n)) & 31u;
        out |= ((c << 3) | (c >> 2)) << (8 * n);
    }
    return out;
}

/* The icons: the list sheet's characters 0x80-0x83 (mission_list.h). */
static int read_graphics(void)
{
    for (int i = 0; i < 4; ++i) {
        if (!khdays_mission_list_character(FIRST_ICON + (unsigned)i, icons[i]))
            return 0;
    }
    colours = khdays_mission_list_colours();
    return colours != NULL;
}

/* --- On the panel ------------------------------------------------------------------------ */

/* Text layer l's map entry at the screen's cell (i, j). */
static uint32_t cell(const KhdaysBgLayer *l, int i, int j)
{
    return khdays_bg_map_entry(1, l, (l->hofs >> 3) + i, (l->vofs >> 3) + j);
}

/* The chests' counter's first cell on the lower screen (DS dots) and its map entry, looked for where
 * the mission panel is (y 152-192, hud.c), with *chests 1. A mission without chests (58, 10, ...) has
 * no counter: then the cell where it would be, a row under the panel's corner, with *chests 0. */
static int find_counter(const KhdaysBgLayer *l, int *x, int *y, uint32_t *entry, int *chests)
{
    const int fx = l->hofs & 7, fy = l->vofs & 7;
    int edge_i = -1, edge_j = -1;
    for (int j = 0; j < 25; ++j) {
        for (int i = 0; i < 33; ++i) {
            const int my = j * 8 - fy + 4;
            const uint32_t e = cell(l, i, j);
            if (my >= 152 && my < 192 && (e & 0x3ffu) == COUNTER_CHARACTER) {
                *x = i * 8 - fx;
                *y = j * 8 - fy;
                *entry = e;
                *chests = 1;
                return 1;
            }
            if (edge_i < 0 && my >= 144 && my < 168 && (e & 0x3ffu) == PANEL_EDGE &&
                (cell(l, i + 1, j) & 0x3ffu) == PANEL_EDGE + 1 && (cell(l, i + 2, j) & 0x3ffu) == PANEL_EDGE + 2) {
                edge_i = i;
                edge_j = j;
            }
        }
    }
    if (edge_i < 0) {
        return 0;
    }
    *x = (edge_i + COUNTER_COLUMN) * 8 - fx;
    *y = (edge_j + 1) * 8 - fy;
    *entry = cell(l, edge_i + COUNTER_COLUMN, edge_j + 1);
    *chests = 0;
    return 1;
}

/* The counter's 4 x 8 glyph `k` (0-9, 10 the slash), as
 * Ov002_BlitPageIconCell copies it: the left half of the k-th 4-bit
 * character of the counter's sheet (the CHAR block at
 * *data_ov002_0207f634 + 0x2c, its characters at +0x14). 0 when the sheet is
 * not there. */
static int glyph(int k, uint8_t out[8][4])
{
    const uint32_t page = *(const volatile uint32_t *)KHDAYS_W(COUNTER_PAGE);
    uint32_t sheet, tiles;
    if (!in_game_ram(page))
        return 0;
    sheet = *(const volatile uint32_t *)KHDAYS_W(page + 0x2c);
    if (!in_game_ram(sheet))
        return 0;
    tiles = *(const volatile uint32_t *)KHDAYS_W(sheet + 0x14);
    if (!in_game_ram(tiles))
        return 0;
    for (int r = 0; r < 8; ++r) {
        for (int b = 0; b < 2; ++b) {
            const uint8_t v =
                *(const volatile uint8_t *)KHDAYS_W(tiles + (uint32_t)k * 32u + (uint32_t)r * 4u + (uint32_t)b);
            out[r][2 * b] = v & 15u;
            out[r][2 * b + 1] = v >> 4;
        }
    }
    return 1;
}

/* What the 24 x 8 image shows: on two screens, a cell per badge (the icons
 * as the list draws them, -1 none); on one screen, a badge and "found/total"
 * in the counter's glyphs and colours, 4 dots apart as the chest icon and its
 * counter are. */
typedef struct Picture {
    int one;
    int cell[3];
    int icon, found, total;
    uint32_t colours[16]; /* the counter's palette, as the picture's colours */
} Picture;

static void paint(const Picture *p, uint32_t *texels, int scale)
{
    uint32_t row[24 * 8];
    memset(row, 0, sizeof(row));
    if (!p->one) {
        for (int c = 0; c < 3; ++c) {
            for (int i = 0; p->cell[c] >= 0 && i < 64; ++i) {
                row[(i / 8) * 24 + c * 8 + i % 8] = colours[icons[p->cell[c]][i]];
            }
        }
    } else {
        const int digits[3] = {p->found, 10, p->total};
        for (int i = 0; i < 64; ++i) {
            row[(i / 8) * 24 + i % 8] = colours[icons[p->icon][i]];
        }
        for (int d = 0; d < 3; ++d) {
            uint8_t g[8][4];
            if (!glyph(digits[d], g))
                break;
            for (int r = 0; r < 8; ++r) {
                for (int c = 0; c < 4; ++c) {
                    if (g[r][c] != 0)
                        row[r * 24 + 12 + d * 4 + c] = p->colours[g[r][c]];
                }
            }
        }
    }
    for (int py = 0; py < 8 * scale; ++py) {
        for (int px = 0; px < 24 * scale; ++px) {
            texels[(size_t)py * (size_t)(24 * scale) + (size_t)px] = row[(py / scale) * 24 + px / scale];
        }
    }
}

static int image = -1, image_scale;
static Picture drawn;
static int drawn_once;

void khdays_ui_badges_emit(void)
{
    const int scale = khdays_ui_scale();
    const uint8_t mode = *(const volatile uint8_t *)KHDAYS_W(MODE_FLAGS);
    const uint8_t *palette = (const uint8_t *)KHDAYS_W(0x05000400u); /* engine B's BG palette */
    KhdaysBgLayer l;
    KhdaysUiElement e;
    Picture now;
    uint16_t room, mission;
    uint32_t context, entry;
    unsigned kinds, found, ranks;
    int language, x, y, chests, changed = 0;
    /* the check compares the game's own drawing, which has none of this */
    if (khdays_ui_mode(KHDAYS_UI_HUD) != KHDAYS_UI_ON || (mode & 2u))
        return;
    if (graphics == 0)
        graphics = read_graphics() ? 1 : -1;
    if (graphics < 0)
        return;
    language = GetLanguage();
    room = *(const volatile uint16_t *)KHDAYS_W(MODE_FLAGS + 2u);
    mission = *(const volatile uint16_t *)KHDAYS_W(MISSION);
    kinds = khdays_mission_badges(language, room, mission);
    if (kinds == 0 || !khdays_bg_text_layer(1, 2, &l) || !find_counter(&l, &x, &y, &entry, &chests))
        return;
    found = (unsigned)GameState_GetField(FOUND_FIELD + 4 * mission, 4);
    context = *(const volatile uint32_t *)KHDAYS_W(FIELD_CONTEXT);
    for (unsigned bit = 0; bit < 4 && in_game_ram(context); ++bit) {
        if (*(const volatile uint8_t *)KHDAYS_W(context + FOUND_HERE + 4u * bit))
            found |= 1u << bit;
    }
    memset(&now, 0, sizeof(now));
    now.one = (khdays_host_graphics_flags() & KHDAYS_HOST_ONE_SCREEN) != 0; /* one screen */
    /* the three cells, as Ov008_DrawMissionRow fills a row's: the ordeal
     * badge and the blazon first (by their count, each lit by bit 0, then
     * 1), then the unity badge (bit 3) */
    ranks = (kinds & 1u) + ((kinds >> 1) & 1u);
    for (unsigned i = 0; i < 3; ++i) {
        now.cell[i] = -1;
        if (i < 2 && i < ranks)
            now.cell[i] = (found >> i) & 1u ? RANK_FOUND : RANK_OPEN;
        if (i == 2 && (kinds & 8u))
            now.cell[i] = (found & 8u) ? UNITY_FOUND : UNITY_OPEN;
        if (now.cell[i] >= 0) {
            now.total += 1;
            now.found += now.cell[i] == RANK_FOUND || now.cell[i] == UNITY_FOUND;
        }
    }
    now.icon = ranks > 0 ? RANK_FOUND : UNITY_FOUND;
    for (int i = 0; i < 16; ++i) {
        const uint32_t at = ((entry >> 12) * 16u + (uint32_t)i) * 2u;
        now.colours[i] = i == 0 ? 0u : rgba_of((uint32_t)(palette[at] | palette[at + 1] << 8) & 0x7fffu);
    }
    if (image < 0 || image_scale != scale) {
        khdays_ui_image_put(image);
        image = khdays_ui_image_get(24 * scale, 8 * scale);
        image_scale = scale;
        changed = 1;
    }
    if (image < 0)
        return;
    changed |= !drawn_once || memcmp(&drawn, &now, sizeof(now)) != 0;
    if (changed) {
        uint32_t *texels = khdays_ui_image_begin(image);
        if (texels == NULL)
            return;
        paint(&now, texels, scale);
        khdays_ui_image_end(image);
        drawn = now;
        drawn_once = 1;
    }
    e.id = "hud.badges";
    e.screen = 1;
    e.slot = KES_DIRECT | 2u; /* above the panel, in its layer (BG2) */
    /* left of the chest icon (4 dots from it on one screen, as its counter is); with no chests, where
     * the icon would be */
    e.x = (float)(x - (!chests ? 8 : now.one ? 36 : 32));
    e.y = (float)y;
    e.w = 24.0f;
    e.h = 8.0f;
    e.image = image;
    khdays_ui_emit(KHDAYS_UI_HUD, &e);
}
