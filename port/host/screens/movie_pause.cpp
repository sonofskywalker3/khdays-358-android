// The pause over an HD movie (movie_pause.h), built from the game's own pieces as PauseMenu_Open lays
// out a scene's pause: pausebg_<lang>.pbg's screen, the title from pauseicon (12 x 2 cells at 10, 8),
// Continue and Skip from etcicon (11 x 2 cells from 0, 0 and 11, 0, at 10, 11 and 10, 13). A lit
// entry is the same cells 2 rows down, its blink 4 rows down (switched every 16 frames).
#include "movie_pause.h"

#include "khdays/assets/graphics2d.h"

extern "C" {
#include "../../runtime/core/rom.h"
#include "../../runtime/core/session_log.h"
}

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

namespace {

using khdays::assets::Tilemap;

constexpr int kW = 256, kH = 192;
const char *const kCodes[6] = {"", "en", "fr", "de", "it", "es"};

struct Pieces {
    int language = 0;
    bool tried = false, ok = false;
    Tilemap base, etc, icon; // the panel's screen, the entries', the title's
    khdays::assets::TileGraphics tiles;
    khdays::assets::Palette2D palette;
};
Pieces pieces;
KhdaysRomReader *reader;
SDL_Renderer *made_for;
SDL_Texture *textures[2][2]; // by the entry lit, then its look (1, 2)

std::vector<std::uint8_t> game_file(const char *path)
{
    u8 *data = nullptr;
    u32 size = 0;
    if (!khdays_rom_file_data(reader, path, -1, &data, &size)) {
        return {};
    }
    std::vector<std::uint8_t> bytes(data, data + size);
    std::free(data);
    return bytes;
}

// A text screen's cell: stored in blocks of 32 x 32 (func_020130b0's CalcTextScreenOffset).
std::size_t cell_at(int x, int y, int w, int h)
{
    const int xb = x / 32, yb = y / 32;
    const int block_w = xb == w / 32 ? w % 32 : 32, block_h = yb == h / 32 ? h % 32 : 32;
    return static_cast<std::size_t>(w * 32 * yb + 32 * block_h * xb + block_w * (y % 32) + x % 32);
}

bool load(int language)
{
    char path[64];
    std::snprintf(path, sizeof(path), "pause/pausebg_%s.pbg.z", kCodes[language]);
    const auto pbg = game_file(path), etc = game_file("pause/etcicon.NSCR.z"),
               icon = game_file("pause/pauseicon.NSCR.z");
    if (pbg.empty() || etc.empty() || icon.empty()) {
        return false;
    }
    try {
        const auto pack = khdays::assets::parse_pk2d(pbg.data(), pbg.size());
        if (pack.screens.empty() || pack.tiles.empty() || pack.palettes.empty()) {
            return false;
        }
        pieces.base = khdays::assets::decode_nscr(pack.screens[0].data, pack.screens[0].size);
        pieces.tiles = khdays::assets::decode_ncgr(pack.tiles[0].data, pack.tiles[0].size);
        pieces.palette = khdays::assets::decode_nclr(pack.palettes[0].data, pack.palettes[0].size);
        pieces.etc = khdays::assets::decode_nscr(etc.data(), etc.size());
        pieces.icon = khdays::assets::decode_nscr(icon.data(), icon.size());
    } catch (const std::exception &e) {
        khdays_log("movies: the pause's pieces cannot be read (%s)", e.what());
        return false;
    }
    const auto whole = [](const Tilemap &m) {
        return m.cells.size() >= static_cast<std::size_t>(m.width_tiles) * static_cast<std::size_t>(m.height_tiles);
    };
    return whole(pieces.base) && whole(pieces.etc) && whole(pieces.icon) && pieces.base.width_tiles * 8 >= kW &&
           pieces.base.height_tiles * 8 >= kH;
}

// Cells w x h from (sx, sy) of `from` to (dx, dy) of `to`, those outside either left out.
void copy(Tilemap &to, const Tilemap &from, int sx, int sy, int dx, int dy, int w, int h)
{
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (sx + x < from.width_tiles && sy + y < from.height_tiles && dx + x < to.width_tiles &&
                dy + y < to.height_tiles) {
                to.cells[cell_at(dx + x, dy + y, to.width_tiles, to.height_tiles)] =
                    from.cells[cell_at(sx + x, sy + y, from.width_tiles, from.height_tiles)];
            }
        }
    }
}

SDL_Texture *make(SDL_Renderer *renderer, int selected, int lit)
{
    Tilemap map = pieces.base;
    copy(map, pieces.icon, 0, 0, 10, 8, 12, 2);
    copy(map, pieces.etc, 0, selected == 0 ? 2 * lit : 0, 10, 11, 11, 2);
    copy(map, pieces.etc, 11, selected == 1 ? 2 * lit : 0, 10, 13, 11, 2);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kW) * kH * 4, 0);
    const auto &tiles = pieces.tiles;
    const auto &colours = pieces.palette.colors;
    for (int cy = 0; cy < kH / 8; ++cy) {
        for (int cx = 0; cx < kW / 8; ++cx) {
            const auto &cell = map.cells[cell_at(cx, cy, map.width_tiles, map.height_tiles)];
            if (cell.tile >= tiles.tile_count) {
                continue;
            }
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    const int tx = cell.flip_h ? 7 - x : x, ty = cell.flip_v ? 7 - y : y;
                    const int index = tiles.indices[static_cast<std::size_t>(cell.tile) * 64 + ty * 8 + tx];
                    const std::size_t colour = tiles.bpp == 4 ? cell.palette * 16u + index : index;
                    if (index == 0 || colour >= colours.size()) {
                        continue; // its colour 0 is clear: the video shows through
                    }
                    std::uint8_t *p = &rgba[(static_cast<std::size_t>(cy * 8 + y) * kW + cx * 8 + x) * 4];
                    p[0] = colours[colour][0];
                    p[1] = colours[colour][1];
                    p[2] = colours[colour][2];
                    p[3] = 255;
                }
            }
        }
    }
    SDL_Texture *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kW, kH);
    if (t != nullptr) {
        SDL_UpdateTexture(t, nullptr, rgba.data(), kW * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    return t;
}

void forget_textures(bool destroy)
{
    for (auto &row : textures) {
        for (auto &t : row) {
            if (t != nullptr && destroy) {
                SDL_DestroyTexture(t);
            }
            t = nullptr;
        }
    }
}

} // namespace

extern "C" SDL_Texture *khdays_movie_pause_panel(SDL_Renderer *renderer, int language, int selected, int lit)
{
    language = language >= 1 && language <= 5 ? language : 1;
    selected = selected != 0 ? 1 : 0;
    lit = lit == 2 ? 2 : 1;
    if (reader == nullptr) {
        reader = khdays_rom_reader_new();
    }
    if (!pieces.tried || pieces.language != language) {
        forget_textures(renderer == made_for);
        pieces.tried = true;
        pieces.language = language;
        pieces.ok = reader != nullptr && load(language);
        if (!pieces.ok) {
            khdays_log("movies: no pause over the HD movie in %s: the game's pause pieces are missing",
                       kCodes[language]);
        }
    }
    if (!pieces.ok) {
        return nullptr;
    }
    if (renderer != made_for) {
        forget_textures(false); // the old renderer's, gone with it
        made_for = renderer;
    }
    SDL_Texture *&t = textures[selected][lit - 1];
    if (t == nullptr) {
        t = make(renderer, selected, lit);
    }
    return t;
}

extern "C" void khdays_movie_pause_renderer_gone(void)
{
    forget_textures(made_for != nullptr); /* the renderer is still there: its textures freed first */
    made_for = nullptr;
}
