// A 2D tile sheet seen assembled and edited as a picture: the sheet as the port names and replaces it,
// the screens, cell banks and palettes in its data, a picture assembled from one of them (the modder's
// choice), and an edited picture taken back into the sheet (docs/STUDIO.md, "2D sheets").
#pragma once

#include "khdays/assets/graphics2d.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace studio {

struct Resource {
    std::size_t at = 0, size = 0;  // in the data
};

// A sheet, its data and what else that data holds.
struct SheetSource {
    std::string label;  // the data (`UI/mlt/res.p2#3`)
    int number = 0;     // its n-th sheet (from 0)
    std::vector<std::uint8_t> data;
    // the sheet as the port takes it (port/runtime/mods/names.cpp read_sheet)
    std::size_t tiles_at = 0;
    int tile_bytes = 32;  // 32: 4 bits a pixel, 64: 8
    int tiles = 0, columns = 0;
    bool shaped = false;  // its header gives its columns and rows (columns * rows == tiles)
    std::vector<Resource> screens, cell_banks, palettes;  // NSCR, NCER, NCLR in the same data
    int sheets_in_data = 0;                               // how many sheets the data holds
    // palettes of other game data near it (load_sheet_source): numbered after
    // the data's own, closest first -- the same entry of the file without
    // its language, the file's other entries, that file's other entries, the
    // folder's palette files by their name
    struct NearbyPalette {
        std::string from;  // `<data>` or `<data> palette <k>`
        std::vector<std::uint8_t> bytes;
        khdays::assets::Palette2D colours;  // with no bytes: the colours seen in play (take_seen_colours)
    };
    std::vector<NearbyPalette> nearby;
    // with no screens or cells in its data: where those it has were taken from
    // (load_sheet_source) -- its twin's `<data>/chr/<n>`, or its folder's
    // screen and cell files; copied after its data, its tiles unmoved
    std::string borrowed;
    // the colours the game was seen drawing it with (take_seen_colours): the
    // palette made of them (-1: none), the palette row each tile was drawn
    // with (-1: not seen), the row for those not seen (its greys) and how
    // many they are
    int seen_palette = -1;
    std::vector<int> seen_rows;
    int seen_row = 0;
    int unseen_tiles = 0;

    int bpp() const {
        return tile_bytes == 64 ? 8 : 4;
    }
    int width() const {
        return columns * 8;
    }
    int height() const {
        return (tiles + columns - 1) / columns * 8;
    }
};

// How many palettes there are to choose from (the data's own and nearby).
int palette_count(const SheetSource& s);

// Where palette `i` is: "" for the data's own, else the nearby data's label.
std::string palette_from(const SheetSource& s, int i);

// The sheet named `key` (`<data>/chr/<n>`) from the extracted game files,
// with the palettes near it.
bool load_sheet_source(const std::filesystem::path& nitrofs, const std::string& key, SheetSource* out,
                       std::string* why);

// The row after a sheet's 16 in the palette of the colours seen in play:
// greys, for the tiles the game was never seen drawing.
constexpr int kUnseenRow = 16;

// Whether tile `tile` of the sheet draws something (a pixel not 0).
bool tile_draws(const SheetSource& s, int tile);

// The colours the game was seen drawing sheet `key` with, from the dump's "<key>.colours"
// (port/runtime/mods/sheet_colours.h), made a palette of `s`: the rows seen as the game had them,
// kUnseenRow's greys after them. False when there is none (docs/STUDIO.md, "2D sheets").
bool take_seen_colours(const std::filesystem::path& dump, const std::string& key, SheetSource* s);

// The same from data already read (sheets found as names.cpp finds them:
// "RGCN" every 4 bytes, valid ones numbered in order).
bool sheet_source(std::vector<std::uint8_t> data, const std::string& label, int number, SheetSource* out,
                  std::string* why);

// The sheet's pixels as the port draws them, at `scale` times its size:
// indexed, each pixel a palette index; else each pixel RGBA (r | g << 8 |
// b << 16 | a << 24).
struct SheetPixels {
    int scale = 1;
    bool indexed = true;
    int width = 0, height = 0;
    std::vector<std::uint32_t> px;
};

// The game's own (indexed, scale 1).
SheetPixels game_sheet_pixels(const SheetSource& s);

// A replacement PNG as tiles2d.c reads it (false with the reason when the
// port would not draw it).
bool read_sheet_png(const std::filesystem::path& png, const SheetSource& s, SheetPixels* out, std::string* why);

// Written as the port reads it; an indexed one shown with `display` (RGBA
// colours, 16 or 256; its index 0 transparent). Empty, or the reason.
std::string write_sheet_png(const std::filesystem::path& png, const SheetPixels& p,
                            const std::vector<std::array<std::uint8_t, 4>>& display);

// What a picture is assembled from: a screen, a cell bank's cells laid out
// side by side, or the sheet as it is (its tiles in its rows, all drawn with
// palette row `row` -- which row the game gives each tile is in its code),
// coloured with a palette (-1: none, greys).
struct Arrangement {
    bool cells = false;
    int resource = 0;  // into screens or cell_banks
    int palette = -1;  // into palettes
    bool whole = false;
    int row = 0;
    // the sheet as it is: a palette row per tile (empty, or -1 for a tile:
    // `row`) -- the game gives the parts of some sheets rows of their own
    // (the HUD portraits: portrait k row k + 1), in its code
    std::vector<int> rows;
};

// The palette rows the modder gave a sheet's tiles (the sheet as it is), kept
// in the mod as `studio/rows/<sheet>.txt`: lines `<first tile>-<last tile>
// <row>`. Empty when there are none.
std::vector<int> read_tile_rows(const std::filesystem::path& mod_dir, const std::string& key, int tiles);
// Written (none given: the file removed); empty or the reason.
std::string write_tile_rows(const std::filesystem::path& mod_dir, const std::string& key, const std::vector<int>& rows);

// The screens and cell banks the sheet can be assembled with (all their tiles in it; fitting is not
// the game's pairing), then the sheet as it is, each with its palette (docs/STUDIO.md, "2D sheets").
std::vector<Arrangement> fitting_arrangements(const SheetSource& s);

// How many palette rows `palette` holds for the sheet (16 colours each, or
// 256 for a 256-colour sheet); at least 1.
int palette_rows(const SheetSource& s, const khdays::assets::Palette2D& palette);

// How many different colours the liveliest row of `palette` gives the
// indices the sheet uses (0: none -- a black or empty palette).
int liveliness(const SheetSource& s, const khdays::assets::Palette2D& palette);

// The row of `palette` with the most different colours at the indices the
// sheet uses (the first of those): the default row of the sheet as it is --
// a palette's unused rows are often all black, and a mask row has two
// colours (`gameover/gameoverbg_*`: rows 0 and 2-15 black, 1 the buttons).
int liveliest_row(const SheetSource& s, const khdays::assets::Palette2D& palette);

// Where each pixel of an assembled picture (at scale 1) comes from.
struct Place {
    std::int32_t tile = -1;     // -1: no tile there
    std::uint8_t x = 0, y = 0;  // in the tile, flips undone
    std::uint8_t row = 0;       // its palette row (4 bits a pixel), or 256-colour palette
    bool flip_h = false, flip_v = false;
};

struct Assembly {
    int width = 0, height = 0;
    std::vector<Place> places;       // width * height
    std::vector<int> uses;           // per sheet tile: how many places (tiles of 8x8) use it
    std::vector<bool> flipped;       // per sheet tile: placed flipped somewhere
    std::vector<int> row_of;         // per sheet tile: the palette row of its first place, -1 none
    std::vector<bool> several_rows;  // per sheet tile: placed with more than one row
    int overlaps = 0;                // pixels two cell pieces cover: the front one (lower OAM number) is shown
    struct Spot {
        int x, y, tile;  // an 8 x 8 place's top left corner and its tile
    };
    std::vector<Spot> spots;
};

bool assemble(const SheetSource& s, const Arrangement& a, Assembly* out, std::string* why);

// Several arrangements of the same sheet one under the other (8 pixels
// apart), edited as one picture; `tops` gets where each starts.
bool assemble_stack(const SheetSource& s, const std::vector<Arrangement>& list, Assembly* out, std::vector<int>* tops,
                    std::string* why);

// The palette `a` names (empty colours: none).
khdays::assets::Palette2D arrangement_palette(const SheetSource& s, const Arrangement& a);

// The assembled picture, RGBA at the pixels' scale (palette index 0 and
// places with no tile transparent).
std::vector<std::uint8_t> render_assembly(const SheetSource& s, const Assembly& a, const SheetPixels& p,
                                          const khdays::assets::Palette2D& palette);

// Sheet tiles drawn in one colour (0xAABBGGRR: red first in memory).
struct TileMark {
    std::vector<int> tiles;
    std::uint32_t rgba = 0;
};

// The assembly's 8x8 places at `scale` on a transparent RGBA picture, to lay
// over its export: each place's border in `grid`, the places of `marks`'
// tiles in their colours (later marks over earlier ones).
std::vector<std::uint8_t> render_tile_grid(const Assembly& a, int scale, std::uint32_t grid,
                                           const std::vector<TileMark>& marks);

// How much a picture (RGBA, at its first scale) changes across the 8-pixel
// tile borders against inside its tiles: about 1 where the tiles join up as
// one picture, well above where they are out of place (an arrangement that
// only fits by its number of tiles); negative when nothing is drawn. Used to
// show the arrangements that join up first -- never as the game's pairing.
float border_jump(const std::vector<std::uint8_t>& rgba, int width, int height);

// How an arrangement draws the sheet: its border jump, the drawn tiles it places and their share,
// whether a screen only lays the tiles in order (as the sheet itself when as wide), and the palette row
// most of its places use.
struct ArrangementScore {
    Arrangement arrangement;
    float jump = -1.0f;
    std::vector<int> tiles;
    float shown = 0.0f;
    bool in_order = false;
    bool as_sheet = false;
    int row = 0;
};
std::vector<ArrangementScore> score_arrangements(const SheetSource& s, const std::vector<Arrangement>& fits,
                                                 const SheetPixels& current);

// `fits` in the order the window shows them, and how many of the first are shown together: ranked by
// how they join up and how much of the sheet they show (docs/STUDIO.md, "2D sheets").
struct Ranked {
    std::vector<Arrangement> order;
    std::size_t together = 1;
};
Ranked rank_arrangements(const SheetSource& s, const std::vector<Arrangement>& fits, const SheetPixels& current);

// The same pixels at another scale (nearest).
SheetPixels rescale(const SheetPixels& p, int scale);

// How an edited picture's colours go into the sheet: automatic keeps the
// game's palettes when every changed pixel is one of its place's palette
// colours, else its own colours; fit takes the nearest palette colour.
enum class Colours { automatic, own, fit };

struct TakeBack {
    SheetPixels sheet;              // to write
    int scale = 1;                  // the picture's: the sheet's
    int changed = 0;                // pixels of the picture changed
    int outside = 0;                // changed where no tile is: not taken
    int off_palette = 0;            // changed pixels no palette colour of their place matches
    std::vector<int> conflicts;     // tiles edited differently in two places
    std::vector<int> repeated;      // changed tiles placed more than once here
    std::vector<int> flipped;       // changed tiles placed flipped somewhere
    std::vector<int> several_rows;  // own colours: tiles drawn with several palettes here
    int unplaced_coloured = 0;      // own colours: tiles not in the picture given its palette's first row
};

// `edited` (RGBA, w x h): the picture `render_assembly` gave at `scale` k,
// edited -- a whole multiple of the assembly's size. `current`: the sheet
// the picture was rendered from (the game's or the mod's). With
// `all_changed` every placed pixel is taken (the whole-game check). False
// with the reason.
bool take_back(const SheetSource& s, const Assembly& a, const SheetPixels& current,
               const khdays::assets::Palette2D& palette, const std::vector<std::uint8_t>& edited, int w, int h,
               Colours colours, bool all_changed, TakeBack* out, std::string* why);

// The sheet as a mod has it (`mods/<Mod>/graphics/<key>.png`, when the port
// would draw it), else the game's; `from` the mod's file or empty.
SheetPixels current_sheet(const std::filesystem::path& mod_dir, const std::string& key, const SheetSource& s,
                          std::filesystem::path* from);

// The colours an indexed sheet PNG is shown with (the port reads only its
// indices): the palette row most of the assembly's tiles use.
std::vector<std::array<std::uint8_t, 4>> display_colours(const SheetSource& s, const Assembly& a,
                                                         const khdays::assets::Palette2D& palette);

// `sheet` written to `mods/<Mod>/graphics/<key>.png` safely, read back as the
// port reads it and compared. False with the reason.
bool save_sheet(const std::filesystem::path& mod_dir, const std::string& key, const SheetSource& s,
                const SheetPixels& sheet, const std::vector<std::array<std::uint8_t, 4>>& display,
                std::filesystem::path* written, std::string* why);

// Every sheet of the game assembled with each arrangement that fits it,
// rendered, taken back whole and rendered again:
// `line` gets the problems and the totals; true when there was none.
bool check_all_sheets(const std::filesystem::path& nitrofs, const std::function<void(const std::string&)>& line);

// What the window first shows of a sheet: the arrangements that fit it,
// ranked by rank_arrangements against the sheet as the mod has it
// (`current`), the first `together` chosen, coloured with the first one's
// palette; the sheet as it is in the row and tile rows its screen -- or the
// colours seen in play -- gives it.
struct SheetChoice {
    std::vector<Arrangement> order;
    std::size_t together = 0;
    int palette = -1;
    int row = 0;
    std::vector<int> screen_rows;
};
SheetChoice default_choice(const SheetSource& s, const SheetPixels& current);

// `a` coloured as chosen: `palette`, the sheet's `row` and, for the sheet as
// it is, each tile's row (`tile_rows`, -1: `row`).
Arrangement coloured_as(Arrangement a, int palette, int row, const std::vector<int>& tile_rows);

// The picture the window first shows of the sheet `key` (RGBA, w x h): the
// sheet as the mod `mod_dir` has it (none: the game's), with the rows its
// modder gave its tiles.
bool default_sheet_picture(const SheetSource& s, const std::filesystem::path& mod_dir, const std::string& key,
                           std::vector<std::uint8_t>* rgba, int* w, int* h, std::string* why);

// Every 2D sheet of the game written into `folder` as "<key>.png", as the window first shows it, with
// the colours seen in play; `each` hears the progress, false stops. How many were written; those that
// could not be, with why, go to `failed`.
int export_sheets(const std::filesystem::path& nitrofs, const std::filesystem::path& dump,
                  const std::filesystem::path& mod_dir, const std::filesystem::path& folder,
                  const std::function<bool(std::size_t, std::size_t, const std::string&)>& each,
                  std::vector<std::string>* failed);

}  // namespace studio
