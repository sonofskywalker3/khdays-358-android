// A 2D sheet assembled and taken back: a synthetic
// sheet of four tiles, a screen using one tile twice and one flipped with two
// palette rows, a cell bank whose pieces overlap; edits taken back into the
// sheet as the port reads it, flags as expected. Every file made here.
#include "test.h"

#include "core/files/paths.h"
#include "core/sheets/sheets.h"

extern "C" {
#include "render/png_io.h"
}

#include <cstdio>
#include <cstring>
#include <fstream>

using namespace studio;

namespace {

using Bytes = std::vector<std::uint8_t>;

void add16(Bytes& d, std::uint32_t v) {
    d.push_back(static_cast<std::uint8_t>(v & 0xFF));
    d.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
void add32(Bytes& d, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
void put32(Bytes& d, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
}

// A Nitro file: magic, byte order mark, version, size, header size, one block.
Bytes nitro(const char* magic, const char* block, const Bytes& body) {
    Bytes d(magic, magic + 4);
    add16(d, 0xFEFF);
    add16(d, 0x0100);
    add32(d, 0);
    add16(d, 0x10);
    add16(d, 1);
    d.insert(d.end(), block, block + 4);
    add32(d, static_cast<std::uint32_t>(body.size() + 8));
    d.insert(d.end(), body.begin(), body.end());
    while (d.size() % 4)
        d.push_back(0);
    put32(d, 8, static_cast<std::uint32_t>(d.size()));
    return d;
}

// Four 4-bit tiles, 2 x 2: tile t's pixel (x, y) is (t * 4 + x + y) % 15 + 1,
// but tile 3 is all index 5.
Bytes ncgr() {
    Bytes b;
    add16(b, 2);  // height in tiles
    add16(b, 2);  // width
    add32(b, 3);  // 4 bits a pixel
    add32(b, 0);
    add32(b, 0);
    add32(b, 4 * 32);  // data size
    add32(b, 0x18);    // data from the block's +0x18 (the file's 0x30)
    for (int t = 0; t < 4; ++t) {
        for (int i = 0; i < 64; i += 2) {
            auto v = [&](int p) { return t == 3 ? 5 : (t * 4 + p % 8 + p / 8) % 15 + 1; };
            b.push_back(static_cast<std::uint8_t>(v(i) | v(i + 1) << 4));
        }
    }
    return nitro("RGCN", "RAHC", b);
}

// A 3 x 2 screen: tile 0, tile 1, tile 1 / tile 2 flipped across, tile 0 row 1, tile 1 row 1.
Bytes nscr() {
    Bytes b;
    add16(b, 24);
    add16(b, 16);
    add16(b, 0);  // 16 colours
    add16(b, 0);  // text
    add32(b, 6 * 2);
    for (const std::uint32_t e : {0u, 1u, 1u, 2u | 0x400u, 0u | 0x1000u, 1u | 0x1000u})
        add16(b, e);
    return nitro("RCSN", "NRCS", b);
}

// Two rows of 16: row 0 index i = (i*16, 0, 0), row 1 index i = (0, i*16, 0).
Bytes nclr() {
    Bytes b;
    add32(b, 3);
    add32(b, 0);
    add32(b, 64);
    add32(b, 0x10);
    for (int row = 0; row < 2; ++row) {
        for (int i = 0; i < 16; ++i) {
            const std::uint32_t v = static_cast<std::uint32_t>(i * 2);  // 5 bits: i * 16 >> 3
            add16(b, row == 0 ? v : v << 5);
        }
    }
    return nitro("RLCN", "TTLP", b);
}

// One cell: a 16 x 8 piece (tiles 0-1) flipped across at (0, 0), then an 8 x 8 piece (tile 3) at (4, 0).
Bytes ncer() {
    Bytes b;
    add16(b, 1);     // cells
    add16(b, 0);     // type
    add32(b, 0x10);  // cell records from the block's data (its +8) + this
    add32(b, 0);     // mapping mode 0: 32-byte boundary
    add32(b, 0);
    // the record: 2 pieces, attributes, their place from the pieces' base
    add16(b, 2);
    add16(b, 0);
    add32(b, 0);
    // piece 0: y 0, shape wide; x 0, size 0, flipped across; tile 0
    add16(b, 0x4000);
    add16(b, 0x1000);
    add16(b, 0);
    // piece 1: y 0, square; x 4, size 0; tile 3
    add16(b, 0);
    add16(b, 4);
    add16(b, 3);
    return nitro("RECN", "KBEC", b);
}

Bytes data() {
    Bytes d;
    for (const Bytes& r : {ncgr(), nscr(), nclr(), ncer()})
        d.insert(d.end(), r.begin(), r.end());
    return d;
}

SheetSource source() {
    SheetSource s;
    std::string why;
    CHECK(sheet_source(data(), "x.p2#0", 0, &s, &why));
    return s;
}

std::uint32_t pixel(const std::vector<std::uint8_t>& rgba, int w, int x, int y) {
    std::uint32_t v = 0;
    std::memcpy(
        &v, &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4], 4);
    return v;
}
void set(std::vector<std::uint8_t>& rgba, int w, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b,
         std::uint8_t a = 255) {
    std::uint8_t* p =
        &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4];
    p[0] = r, p[1] = g, p[2] = b, p[3] = a;
}
std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint32_t>(r) | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(b) << 16 |
           0xFF000000u;
}
// the 5-bit palette value i * 2 shown at 8 bits
std::uint8_t level(int i) {
    const int v = i * 2;
    return static_cast<std::uint8_t>(v << 3 | v >> 2);
}
bool contains(const std::vector<int>& v, int x) {
    for (const int e : v) {
        if (e == x)
            return true;
    }
    return false;
}

}  // namespace

TEST(sheet_found_as_the_port_numbers_it) {
    const SheetSource s = source();
    CHECK(s.tiles == 4 && s.columns == 2 && s.bpp() == 4);
    CHECK(s.screens.size() == 1 && s.palettes.size() == 1 && s.cell_banks.size() == 1);
    const auto fit = fitting_arrangements(s);
    CHECK(fit.size() == 3);
    CHECK(!fit[0].cells && fit[0].palette == 0 && fit[1].cells && fit[2].whole && fit[2].palette == 0);
    SheetSource none;
    CHECK(!sheet_source(data(), "x.p2#0", 1, &none, nullptr));
}

TEST(screen_assembled_with_its_flips_and_rows) {
    const SheetSource s = source();
    Assembly a;
    CHECK(assemble(s, {false, 0, 0}, &a, nullptr));
    CHECK(a.width == 24 && a.height == 16);
    CHECK(a.uses[0] == 2 && a.uses[1] == 3 && a.uses[2] == 1 && a.uses[3] == 0);
    CHECK(a.flipped[2] && !a.flipped[1]);
    CHECK(a.several_rows[0] && a.several_rows[1] && !a.several_rows[2]);
    const auto pal = arrangement_palette(s, {false, 0, 0});
    const auto pic = render_assembly(s, a, game_sheet_pixels(s), pal);
    // tile 0 at (0, 0), pixel (1, 0): index 2, row 0
    CHECK(pixel(pic, 24, 1, 0) == rgba(level(2), 0, 0));
    // tile 2 flipped across at (0, 8): its pixel (7, 0) is drawn at (0, 8): index (8 + 7) % 15 + 1 = 1
    CHECK(pixel(pic, 24, 0, 8) == rgba(level(1), 0, 0));
    // tile 0 with row 1 at (8, 8): green
    CHECK(pixel(pic, 24, 9, 8) == rgba(0, level(2), 0));
}

TEST(an_edit_in_the_game_palette_stays_indexed) {
    const SheetSource s = source();
    Assembly a;
    CHECK(assemble(s, {false, 0, 0}, &a, nullptr));
    const auto pal = arrangement_palette(s, {false, 0, 0});
    const SheetPixels game = game_sheet_pixels(s);
    auto pic = render_assembly(s, a, game, pal);
    // on the flipped tile 2, at the picture's (0, 9): its own pixel (7, 1); now index 9 of row 0
    set(pic, 24, 0, 9, level(9), 0, 0);
    TakeBack t;
    std::string why;
    CHECK(take_back(s, a, game, pal, pic, 24, 16, Colours::automatic, false, &t, &why));
    CHECK(t.changed == 1 && t.off_palette == 0 && t.sheet.indexed && t.scale == 1);
    // tile 2 sits at the sheet's (0, 8)
    CHECK(t.sheet.px[static_cast<std::size_t>(9) * 16 + 7] == 9);
    CHECK(contains(t.flipped, 2) && t.conflicts.empty());
    for (std::size_t i = 0; i < game.px.size(); ++i) {
        if (i != static_cast<std::size_t>(9) * 16 + 7)
            CHECK(t.sheet.px[i] == game.px[i]);
    }
    // a pixel made transparent is index 0
    auto clear = render_assembly(s, a, game, pal);
    set(clear, 24, 1, 0, 0, 0, 0, 0);
    CHECK(take_back(s, a, game, pal, clear, 24, 16, Colours::automatic, false, &t, &why));
    CHECK(t.sheet.indexed && t.sheet.px[1] == 0);
}

TEST(a_repeated_tile_changed_once_or_twice) {
    const SheetSource s = source();
    Assembly a;
    CHECK(assemble(s, {false, 0, 0}, &a, nullptr));
    const auto pal = arrangement_palette(s, {false, 0, 0});
    const SheetPixels game = game_sheet_pixels(s);
    // tile 1 at (8, 0) and (16, 0): one copy changed -- the change taken, flagged as shown twice
    auto once = render_assembly(s, a, game, pal);
    set(once, 24, 18, 3, level(14), 0, 0);
    TakeBack t;
    CHECK(take_back(s, a, game, pal, once, 24, 16, Colours::automatic, false, &t, nullptr));
    CHECK(t.conflicts.empty() && contains(t.repeated, 1));
    CHECK(t.sheet.px[static_cast<std::size_t>(3) * 16 + 8 + 2] == 14);
    // both copies changed differently: a conflict, the first kept
    auto twice = once;
    set(twice, 24, 10, 3, level(13), 0, 0);
    CHECK(take_back(s, a, game, pal, twice, 24, 16, Colours::automatic, false, &t, nullptr));
    CHECK(contains(t.conflicts, 1));
    CHECK(t.sheet.px[static_cast<std::size_t>(3) * 16 + 8 + 2] == 13);
}

TEST(new_colours_make_the_sheet_its_own_or_fit_the_palette) {
    const SheetSource s = source();
    Assembly a;
    CHECK(assemble(s, {false, 0, 0}, &a, nullptr));
    const auto pal = arrangement_palette(s, {false, 0, 0});
    const SheetPixels game = game_sheet_pixels(s);
    auto pic = render_assembly(s, a, game, pal);
    set(pic, 24, 0, 9, 200, 100, 50);  // no colour of the palette
    TakeBack own;
    CHECK(take_back(s, a, game, pal, pic, 24, 16, Colours::automatic, false, &own, nullptr));
    CHECK(!own.sheet.indexed && own.off_palette == 1);
    CHECK(own.sheet.px[static_cast<std::size_t>(9) * 16 + 7] == rgba(200, 100, 50));
    CHECK(contains(own.several_rows, 0) && contains(own.several_rows, 1));
    CHECK(own.unplaced_coloured == 1);  // tile 3, in no place, has colours
    // the rest as the game draws it, with each tile's first row
    CHECK(own.sheet.px[1] == rgba(level(2), 0, 0));
    TakeBack fit;
    CHECK(take_back(s, a, game, pal, pic, 24, 16, Colours::fit, false, &fit, nullptr));
    CHECK(fit.sheet.indexed &&
          fit.sheet.px[static_cast<std::size_t>(9) * 16 + 7] == 12);  // nearest red: 200 ~ index 12
}

TEST(an_edit_at_twice_the_size) {
    const SheetSource s = source();
    Assembly a;
    CHECK(assemble(s, {false, 0, 0}, &a, nullptr));
    const auto pal = arrangement_palette(s, {false, 0, 0});
    const SheetPixels game = game_sheet_pixels(s);
    const SheetPixels big = rescale(game, 2);
    auto pic = render_assembly(s, a, big, pal);
    // one of the four pixels of the flipped tile 2's (7, 0), drawn at (0..1, 16..17): the left one
    set(pic, 48, 0, 16, level(6), 0, 0);
    TakeBack t;
    CHECK(take_back(s, a, game, pal, pic, 48, 32, Colours::automatic, false, &t, nullptr));
    CHECK(t.scale == 2 && t.sheet.scale == 2 && t.sheet.width == 32 && t.changed == 1);
    // flipped: the picture's left half of that pixel is the sheet's right half
    CHECK(t.sheet.px[static_cast<std::size_t>(16) * 32 + 15] == 6);
    CHECK(t.sheet.px[static_cast<std::size_t>(16) * 32 + 14] == big.px[static_cast<std::size_t>(16) * 32 + 14]);
    std::string why;
    CHECK(!take_back(s, a, game, pal, pic, 47, 32, Colours::automatic, false, &t, &why));
}

TEST(cells_laid_out_the_front_piece_first) {
    const SheetSource s = source();
    Assembly a;
    CHECK(assemble(s, {true, 0, 0}, &a, nullptr));
    CHECK(a.width == 16 && a.height == 8);
    CHECK(a.overlaps == 64);  // the 8 x 8 piece wholly over the wide one
    CHECK(a.uses[0] == 1 && a.uses[1] == 1 && a.uses[3] == 1 && a.flipped[0] && a.flipped[1]);
    // at (5, 0) the front piece (the first, flipped across): its tile 0 is on the right, so (5, 0) is tile 1's (2, 0)
    const Place& p = a.places[5];
    CHECK(p.tile == 1 && p.x == 2 && p.y == 0 && p.flip_h);
}

TEST(sheet_png_written_and_read_as_the_port_reads_it) {
    const SheetSource s = source();
    const auto dir = studio_test::scratch("sheets");
    SheetPixels game = rescale(game_sheet_pixels(s), 3);
    std::vector<std::array<std::uint8_t, 4>> display(16, {1, 2, 3, 255});
    CHECK(write_sheet_png(dir / "indexed.png", game, display).empty());
    SheetPixels back;
    std::string why;
    CHECK(read_sheet_png(dir / "indexed.png", s, &back, &why));
    CHECK(back.indexed && back.scale == 3 && back.px == game.px);
    SheetPixels own = game;
    own.indexed = false;
    for (auto& v : own.px)
        v = rgba(static_cast<std::uint8_t>(v * 10), 7, 9);
    CHECK(write_sheet_png(dir / "own.png", own, display).empty());
    CHECK(read_sheet_png(dir / "own.png", s, &back, &why));
    CHECK(!back.indexed && back.px == own.px);
    // the mod's sheet is the current one, else the game's
    std::filesystem::create_directories(dir / "mod" / "graphics" / "x.p2#0" / "chr");
    CHECK(write_sheet_png(dir / "mod" / "graphics" / "x.p2#0" / "chr" / "0.png", own, display).empty());
    std::filesystem::path from;
    CHECK(current_sheet(dir / "mod", "x.p2#0/chr/0", s, &from).px == own.px && !from.empty());
    CHECK(current_sheet(dir / "other", "x.p2#0/chr/0", s, &from).px == game_sheet_pixels(s).px && from.empty());
}

TEST(arrangements_stacked_in_one_picture) {
    const SheetSource s = source();
    Assembly a;
    std::vector<int> tops;
    std::string why;
    CHECK(assemble_stack(s, {{false, 0, 0}, {true, 0, 0}}, &a, &tops, &why));
    CHECK(a.width == 24 && a.height == 16 + 8 + 8);
    CHECK(tops.size() == 2 && tops[0] == 0 && tops[1] == 24);
    CHECK(a.uses[0] == 3 && a.uses[3] == 1);  // tile 0: twice on the screen, once in the cell
    // the cell's front piece at (5, 24): tile 1's (2, 0), as when assembled alone
    const Place& p = a.places[static_cast<std::size_t>(24) * 24 + 5];
    CHECK(p.tile == 1 && p.x == 2 && p.flip_h);
    // nothing in the gap
    CHECK(a.places[static_cast<std::size_t>(18) * 24 + 3].tile < 0);
    // an edit in the second band reaches the cell's tile
    const auto pal = arrangement_palette(s, {false, 0, 0});
    const SheetPixels game = game_sheet_pixels(s);
    auto pic = render_assembly(s, a, game, pal);
    set(pic, 24, 5, 24, level(11), 0, 0);
    TakeBack t;
    CHECK(take_back(s, a, game, pal, pic, 24, 32, Colours::automatic, false, &t, &why));
    CHECK(t.changed == 1 && t.sheet.px[static_cast<std::size_t>(0) * 16 + 8 + 2] == 11);
    CHECK(!assemble_stack(s, {}, &a, &tops, &why));
}

TEST(the_sheet_as_it_is_with_a_chosen_row) {
    const SheetSource s = source();
    Arrangement whole;
    whole.whole = true;
    whole.palette = 0;
    whole.row = 1;
    Assembly a;
    CHECK(assemble(s, whole, &a, nullptr));
    CHECK(a.width == 16 && a.height == 16 && a.uses[3] == 1 && !a.flipped[2]);
    const auto pal = arrangement_palette(s, whole);
    CHECK(palette_rows(s, pal) == 2);
    const SheetPixels game = game_sheet_pixels(s);
    auto pic = render_assembly(s, a, game, pal);
    // tile 0's pixel (1, 0), index 2, in row 1: green
    CHECK(pixel(pic, 16, 1, 0) == rgba(0, level(2), 0));
    // an edit in row 1's colours stays indexed and touches only that pixel
    set(pic, 16, 9, 9, 0, level(7), 0);  // tile 3 at (8, 8)
    TakeBack t;
    CHECK(take_back(s, a, game, pal, pic, 16, 16, Colours::automatic, false, &t, nullptr));
    CHECK(t.sheet.indexed && t.changed == 1 && t.sheet.px[static_cast<std::size_t>(9) * 16 + 9] == 7);
}

namespace {

// Four 4-bit tiles, 2 x 2, whose rows ramp across each pair: tiles 0 and 2
// pixel (x, y) x + 1, tiles 1 and 3 x + 8.
Bytes ramp_ncgr() {
    Bytes b;
    add16(b, 2);
    add16(b, 2);
    add32(b, 3);
    add32(b, 0);
    add32(b, 0);
    add32(b, 4 * 32);
    add32(b, 0x18);
    for (int t = 0; t < 4; ++t) {
        for (int i = 0; i < 64; i += 2) {
            auto v = [&](int p) { return p % 8 + (t % 2 == 0 ? 1 : 8); };
            b.push_back(static_cast<std::uint8_t>(v(i) | v(i + 1) << 4));
        }
    }
    return nitro("RGCN", "RAHC", b);
}

// A screen of `w` x `h` tiles from its entries as they are stored.
Bytes screen_of(int w, int h, const std::vector<std::uint32_t>& entries) {
    Bytes b;
    add16(b, static_cast<std::uint32_t>(w * 8));
    add16(b, static_cast<std::uint32_t>(h * 8));
    add16(b, 0);
    add16(b, 0);
    add32(b, static_cast<std::uint32_t>(entries.size() * 2));
    for (const std::uint32_t e : entries)
        add16(b, e);
    return nitro("RCSN", "NRCS", b);
}

// The ramp's tiles 1 and 0, both flipped across: a ramp down that joins up.
Bytes ramp_nscr() {
    return screen_of(2, 1, {1u | 0x400u, 0u | 0x400u});
}

}  // namespace

TEST(data_in_threes_pairs_by_number) {
    // two sheets, two screens, two palettes: sheet 1 takes palette 1 and screen 1 first
    Bytes d;
    for (const Bytes& r : {ramp_ncgr(), ramp_ncgr(), ramp_nscr(), ramp_nscr(), nclr(), nclr()})
        d.insert(d.end(), r.begin(), r.end());
    SheetSource s;
    std::string why;
    CHECK(sheet_source(d, "x.p2#0", 1, &s, &why));
    CHECK(s.sheets_in_data == 2 && s.number == 1);
    const auto fits = fitting_arrangements(s);
    CHECK(fits.size() == 3 && fits[0].palette == 1);
    const auto ranked = rank_arrangements(s, fits, game_sheet_pixels(s)).order;
    CHECK(!ranked[0].whole && !ranked[0].cells && ranked[0].resource == 1 && ranked.back().whole);
    // sheet 0: palette 0, screen 0 first
    SheetSource first;
    CHECK(sheet_source(d, "x.p2#0", 0, &first, &why));
    CHECK(fitting_arrangements(first)[0].palette == 0);
    CHECK(rank_arrangements(first, fitting_arrangements(first), game_sheet_pixels(first)).order[0].resource == 0);
}

TEST(border_jump_tells_joined_tiles_from_scattered_ones) {
    // a smooth ramp joins up across tile borders; the same tiles shuffled do not
    const int w = 32, h = 8;
    std::vector<std::uint8_t> ramp(static_cast<std::size_t>(w) * h * 4), shuffled(ramp.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &ramp[(static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4];
            p[0] = static_cast<std::uint8_t>(x * 8), p[1] = 0, p[2] = 0, p[3] = 255;
            const int tile = x / 8, swapped = (tile + 2) % 4;  // tiles moved two places on
            std::uint8_t* q =
                &shuffled[(static_cast<std::size_t>(y) * w + static_cast<std::size_t>(swapped * 8 + x % 8)) * 4];
            std::memcpy(q, p, 4);
        }
    }
    const float joined = border_jump(ramp, w, h), scattered = border_jump(shuffled, w, h);
    CHECK(joined > 0.5f && joined < 1.5f);
    CHECK(scattered > 3.0f);
    CHECK(border_jump(std::vector<std::uint8_t>(ramp.size(), 0), w, h) < 0);  // nothing drawn
}

TEST(the_sheet_as_it_is_starts_at_its_liveliest_row) {
    // a palette whose row 0 is all black and row 1 a ramp: the sheet as it is starts at row 1
    Bytes b;
    add32(b, 3);
    add32(b, 0);
    add32(b, 64);
    add32(b, 0x10);
    for (int i = 0; i < 16; ++i)
        add16(b, 0);
    for (int i = 0; i < 16; ++i)
        add16(b, static_cast<std::uint32_t>(i * 2));
    Bytes d;
    for (const Bytes& r : {ncgr(), nitro("RLCN", "TTLP", b)})
        d.insert(d.end(), r.begin(), r.end());
    SheetSource s;
    CHECK(sheet_source(d, "x.p2#0", 0, &s, nullptr));
    const auto fits = fitting_arrangements(s);
    CHECK(fits.size() == 1 && fits[0].whole && fits[0].row == 1);
    CHECK(liveliest_row(s, arrangement_palette(s, fits[0])) == 1);
}

TEST(a_screen_wider_than_32_tiles_is_read_in_strips) {
    // 33 x 2 tiles: strip 0 holds places (0-31, 0-1) row by row, strip 1 (32, 0-1); entry 64 is place (32, 0)
    std::vector<std::uint32_t> entries(66, 0);
    entries[64] = 1;
    Bytes d;
    for (const Bytes& r : {ramp_ncgr(), screen_of(33, 2, entries), nclr()})
        d.insert(d.end(), r.begin(), r.end());
    SheetSource s;
    CHECK(sheet_source(d, "x.p2#0", 0, &s, nullptr));
    Arrangement screen;
    screen.palette = 0;
    Assembly a;
    CHECK(assemble(s, screen, &a, nullptr));
    CHECK(a.width == 33 * 8 && a.height == 16);
    CHECK(a.places[32 * 8].tile == 1);                                          // (32, 0)
    CHECK(a.places[static_cast<std::size_t>(8) * a.width + 31 * 8].tile == 0);  // (31, 1), row by row's place
}

TEST(a_screen_laying_the_tiles_as_the_sheet_does_stands_for_the_sheet) {
    // the ramp's 2 columns, a screen 2 wide holding tiles 0 and 1 in order: the sheet as it is first, alone
    Bytes d;
    for (const Bytes& r : {ramp_ncgr(), screen_of(2, 1, {0u, 1u}), nclr()})
        d.insert(d.end(), r.begin(), r.end());
    SheetSource s;
    CHECK(sheet_source(d, "x.p2#0", 0, &s, nullptr));
    const auto fits = fitting_arrangements(s);
    const auto scores = score_arrangements(s, fits, game_sheet_pixels(s));
    CHECK(scores.size() == 2 && scores[0].in_order && scores[0].as_sheet && scores[0].shown == 0.5f);
    const Ranked ranked = rank_arrangements(s, fits, game_sheet_pixels(s));
    CHECK(ranked.order[0].whole && ranked.together == 1);
}

TEST(screens_sharing_out_a_sheet_are_shown_together) {
    // screens 0 and 1 join up from tiles 0-1 and 2-3; screen 2 draws tiles 0-1 again
    Bytes d;
    for (const Bytes& r :
         {ramp_ncgr(), ramp_nscr(), screen_of(2, 1, {3u | 0x400u, 2u | 0x400u}), ramp_nscr(), nclr()}) {
        d.insert(d.end(), r.begin(), r.end());
    }
    SheetSource s;
    CHECK(sheet_source(d, "x.p2#0", 0, &s, nullptr));
    const Ranked ranked = rank_arrangements(s, fitting_arrangements(s), game_sheet_pixels(s));
    CHECK(ranked.together == 2 && ranked.order.size() == 4);
    CHECK(!ranked.order[0].whole && ranked.order[0].resource == 0 && ranked.order[1].resource == 1);
    CHECK(ranked.order[2].resource == 2 && ranked.order[3].whole);
}

TEST(a_sheet_with_no_screens_borrows_its_twins_or_its_folders) {
    const auto nitrofs = studio_test::scratch("sheets_borrow");
    auto put = [&](const std::string& path, std::initializer_list<Bytes> parts) {
        Bytes d;
        for (const Bytes& r : parts)
            d.insert(d.end(), r.begin(), r.end());
        studio_test::write(nitrofs / path, std::string(d.begin(), d.end()));
    };
    // x/res_de.bin: the ramp's tiles alone; x/res.bin: the same tiles with a screen and a palette
    put("x/res_de.bin", {ramp_ncgr()});
    put("x/res.bin", {ramp_ncgr(), ramp_nscr(), nclr()});
    SheetSource s;
    std::string why;
    CHECK(load_sheet_source(nitrofs, "x/res_de.bin/chr/0", &s, &why));
    CHECK(s.borrowed == "x/res.bin/chr/0" && s.screens.size() == 1 && s.palettes.empty());
    CHECK(!s.nearby.empty() && s.nearby[0].from == "x/res.bin");
    const auto fits = fitting_arrangements(s);
    CHECK(fits.size() == 2 && !fits[0].whole && fits[0].palette == 0);
    Assembly a;
    CHECK(assemble(s, fits[0], &a, nullptr) && a.width == 16 && a.uses[0] == 1 && a.uses[1] == 1);
    // a sheet of the same size sharing none of its drawn tiles is no twin
    Bytes other = ramp_ncgr();
    for (std::size_t i = 0x30; i < other.size(); ++i)
        other[i] = static_cast<std::uint8_t>(other[i] ^ 0x11);
    put("y/res_de.bin", {other});
    put("y/res.bin", {ramp_ncgr(), ramp_nscr(), nclr()});
    SheetSource lone;
    CHECK(load_sheet_source(nitrofs, "y/res_de.bin/chr/0", &lone, &why));
    CHECK(lone.borrowed.empty() && lone.screens.empty());
    // a file of its own: the folder's screen files
    put("z/bg1.NCGR", {ramp_ncgr()});
    put("z/bg.NSCR", {ramp_nscr()});
    SheetSource file;
    CHECK(load_sheet_source(nitrofs, "z/bg1.NCGR/chr/0", &file, &why));
    CHECK(file.borrowed == "z/bg.NSCR" && file.screens.size() == 1);
}

TEST(the_sheet_as_it_is_with_a_row_per_tile) {
    // tile 1 in row 1 (green), the others in row 0 (red): an edit in each tile's colours stays indexed
    const SheetSource s = source();
    Arrangement whole;
    whole.whole = true;
    whole.palette = 0;
    whole.rows = {-1, 1, -1, -1};
    Assembly a;
    CHECK(assemble(s, whole, &a, nullptr));
    CHECK(a.row_of[0] == 0 && a.row_of[1] == 1 && a.row_of[3] == 0);
    const auto pal = arrangement_palette(s, whole);
    const SheetPixels game = game_sheet_pixels(s);
    auto pic = render_assembly(s, a, game, pal);
    // tile 1 sits at (8, 0): its pixel (1, 0), index (4 + 1) % 15 + 1 = 6, green
    CHECK(pixel(pic, 16, 9, 0) == rgba(0, level(6), 0));
    CHECK(pixel(pic, 16, 1, 0) == rgba(level(2), 0, 0));
    set(pic, 16, 9, 0, 0, level(9), 0);  // tile 1, row 1's index 9
    set(pic, 16, 1, 0, level(4), 0, 0);  // tile 0, row 0's index 4
    TakeBack t;
    CHECK(take_back(s, a, game, pal, pic, 16, 16, Colours::automatic, false, &t, nullptr));
    CHECK(t.sheet.indexed && t.changed == 2 && t.off_palette == 0);
    CHECK(t.sheet.px[9] == 9 && t.sheet.px[1] == 4);
}

TEST(tile_rows_kept_in_the_mod) {
    const auto mod = studio_test::scratch("sheets_rows");
    const std::string key = "UI/x.p2#1/chr/0";
    CHECK(read_tile_rows(mod, key, 6).empty());
    CHECK(write_tile_rows(mod, key, {-1, 2, 2, 2, -1, 13}).empty());
    CHECK(std::filesystem::is_regular_file(mod / "studio" / "rows" / "UI" / "x.p2#1" / "chr" / "0.txt"));
    CHECK((read_tile_rows(mod, key, 6) == std::vector<int>{-1, 2, 2, 2, -1, 13}));
    CHECK((read_tile_rows(mod, key, 4) == std::vector<int>{-1, 2, 2, 2}));  // past the sheet: left out
    CHECK(write_tile_rows(mod, key, {-1, -1}).empty());                     // none: the file goes
    CHECK(read_tile_rows(mod, key, 6).empty() &&
          !std::filesystem::exists(mod / "studio" / "rows" / "UI" / "x.p2#1" / "chr" / "0.txt"));
}

TEST(the_sheet_as_it_is_takes_the_rows_of_the_screen_standing_for_it) {
    // a screen laying the ramp's four tiles as the sheet does, tiles 1 and 3 in row 1 (two rows of
    // reds a step apart, so the picture still joins up)
    Bytes b;
    add32(b, 3);
    add32(b, 0);
    add32(b, 64);
    add32(b, 0x10);
    for (int row = 0; row < 2; ++row) {
        for (int i = 0; i < 16; ++i)
            add16(b, static_cast<std::uint32_t>(i * 2 + row));
    }
    Bytes d;
    for (const Bytes& r :
         {ramp_ncgr(), screen_of(2, 2, {0u, 1u | 0x1000u, 2u, 3u | 0x1000u}), nitro("RLCN", "TTLP", b)}) {
        d.insert(d.end(), r.begin(), r.end());
    }
    SheetSource s;
    CHECK(sheet_source(d, "x.p2#0", 0, &s, nullptr));
    const Ranked ranked = rank_arrangements(s, fitting_arrangements(s), game_sheet_pixels(s));
    CHECK(ranked.together == 1 && ranked.order[0].whole);
    CHECK((ranked.order[0].rows == std::vector<int>{0, 1, 0, 1}));
}

namespace {

// The port's record of the colours a sheet was drawn with (port/runtime/mods/sheet_colours.h):
// tiles 0-1 in row 3 (blues), tile 2 in row 0 (greys), tile 3 never seen.
void write_seen(const std::filesystem::path& dump, const std::string& key) {
    std::filesystem::create_directories((dump / (key + ".colours")).parent_path());
    std::ofstream out(dump / (key + ".colours"));
    out << "# the palette rows the game drew this sheet's tiles with, and their colours (BGR555)\n";
    char word[8];
    for (const int row : {0, 3}) {
        out << "row " << row;
        for (int i = 0; i < 16; ++i) {
            const unsigned v = static_cast<unsigned>(i * 2);
            std::snprintf(word, sizeof(word), " %04x", row == 3 ? v << 10 : v | v << 5 | v << 10);
            out << word;
        }
        out << "\n";
    }
    out << "0-1 3\n2-2 0\n";
}

}  // namespace

TEST(the_colours_seen_in_play_colour_the_sheet) {
    const auto dump = studio_test::scratch("sheets_seen");
    const std::string key = "x.bin/chr/0";
    SheetSource s;
    CHECK(sheet_source(ncgr(), "x.bin", 0, &s, nullptr));  // no screen, no cells, no palette
    CHECK(!take_seen_colours(dump, key, &s));              // none seen yet
    write_seen(dump, key);
    CHECK(take_seen_colours(dump, key, &s));
    CHECK(s.seen_palette == 0 && palette_count(s) == 1);
    CHECK((s.seen_rows == std::vector<int>{3, 3, 0, -1}) && s.seen_row == kUnseenRow && s.unseen_tiles == 1);
    const auto fits = fitting_arrangements(s);
    CHECK(fits.size() == 1 && fits[0].whole && fits[0].palette == 0 && fits[0].row == kUnseenRow &&
          fits[0].rows == s.seen_rows);
    std::vector<std::uint8_t> pic;
    int w = 0, h = 0;
    std::string why;
    CHECK(default_sheet_picture(s, {}, key, &pic, &w, &h, &why));
    CHECK(w == 16 && h == 16);
    // tile 0's pixel (0, 0): index 1 in row 3; tile 2's (0, 8): index 9 in row 0; tile 3, never seen:
    // index 5 in the greys, no colour guessed
    CHECK(pixel(pic, 16, 0, 0) == rgba(0, 0, level(1)));
    CHECK(pixel(pic, 16, 0, 8) == rgba(level(9), level(9), level(9)));
    CHECK(pixel(pic, 16, 8, 8) == rgba(5 * 17, 5 * 17, 5 * 17));
}

TEST(the_colours_seen_of_an_8_bit_sheet) {
    // two 8-bit tiles: tile 0 pixel i is index i + 1, tile 1 all index 7; tile 0 seen in extended
    // palette 2, whose index n is (n % 32) red
    Bytes b;
    add16(b, 1);
    add16(b, 2);
    add32(b, 4);  // 8 bits a pixel
    add32(b, 0);
    add32(b, 0);
    add32(b, 2 * 64);
    add32(b, 0x18);
    for (int i = 0; i < 64; ++i)
        b.push_back(static_cast<std::uint8_t>(i + 1));
    for (int i = 0; i < 64; ++i)
        b.push_back(7);
    SheetSource s;
    CHECK(sheet_source(nitro("RGCN", "RAHC", b), "y.bin", 0, &s, nullptr));
    CHECK(s.bpp() == 8 && s.tiles == 2);
    const auto dump = studio_test::scratch("sheets_seen8");
    const std::string key = "y.bin/chr/0";
    std::filesystem::create_directories((dump / (key + ".colours")).parent_path());
    {
        std::ofstream out(dump / (key + ".colours"));
        out << "row 2";
        char word[8];
        for (int n = 0; n < 256; ++n) {
            std::snprintf(word, sizeof(word), " %04x", static_cast<unsigned>(n % 32));
            out << word;
        }
        out << "\n0-0 2\n";
    }
    CHECK(take_seen_colours(dump, key, &s));
    CHECK((s.seen_rows == std::vector<int>{2, -1}) && s.unseen_tiles == 1);
    std::vector<std::uint8_t> pic;
    int w = 0, h = 0;
    CHECK(default_sheet_picture(s, {}, key, &pic, &w, &h, nullptr));
    CHECK(w == 16 && h == 8);
    // tile 0's pixel (2, 0): index 3, red 3 at 5 bits; tile 1, never seen: index 7 of 256 greys
    CHECK(pixel(pic, 16, 2, 0) == rgba(static_cast<std::uint8_t>(3 << 3 | 3 >> 2), 0, 0));
    CHECK(pixel(pic, 16, 8, 0) == rgba(7, 7, 7));
}

TEST(a_row_with_other_colours_for_other_tiles) {
    // tiles 0-1 drawn in row 2 with one set of colours, tile 2 in row 2 with others: the port
    // writes those others as row 17 (16 is the greys)
    const auto dump = studio_test::scratch("sheets_seen_extra");
    const std::string key = "z.bin/chr/0";
    std::filesystem::create_directories((dump / (key + ".colours")).parent_path());
    {
        std::ofstream out(dump / (key + ".colours"));
        char word[8];
        for (const int row : {2, 17}) {
            out << "row " << row;
            for (int i = 0; i < 16; ++i) {
                const unsigned v = static_cast<unsigned>(i * 2);
                std::snprintf(word, sizeof(word), " %04x", row == 2 ? v : v << 5);
                out << word;
            }
            out << "\n";
        }
        out << "0-1 2\n2-2 17\n";
    }
    SheetSource s;
    CHECK(sheet_source(ncgr(), "z.bin", 0, &s, nullptr));
    CHECK(take_seen_colours(dump, key, &s));
    CHECK((s.seen_rows == std::vector<int>{2, 2, 17, -1}));
    std::vector<std::uint8_t> pic;
    int w = 0, h = 0;
    CHECK(default_sheet_picture(s, {}, key, &pic, &w, &h, nullptr));
    // tile 0's (0, 0): index 1, red; tile 2's (0, 8): index 9, green (its own row)
    CHECK(pixel(pic, 16, 0, 0) == rgba(level(1), 0, 0));
    CHECK(pixel(pic, 16, 0, 8) == rgba(0, level(9), 0));
}

TEST(every_sheet_exported_coloured) {
    const auto root = studio_test::scratch("sheets_export");
    const auto nitrofs = root / "nitrofs", dump = root / "dump", out = root / "out";
    std::filesystem::create_directories(nitrofs);
    {
        const Bytes d = ncgr();
        std::ofstream f(nitrofs / "x.bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
    }
    write_seen(dump, "x.bin/chr/0");
    std::vector<std::string> written, failed;
    const int n = export_sheets(
        nitrofs, dump, {}, out,
        [&](std::size_t, std::size_t, const std::string& key) {
            if (!key.empty())
                written.push_back(key);
            return true;
        },
        &failed);
    CHECK(n == 1 && failed.empty() && (written == std::vector<std::string>{"x.bin/chr/0"}));
    int w = 0, h = 0;
    const char* error = nullptr;
    std::uint8_t* px = khdays_png_read(studio::u8(out / "x.bin" / "chr" / "0.png").c_str(), &w, &h, &error);
    CHECK(px != nullptr && w == 16 && h == 16);
    if (px != nullptr) {
        std::uint32_t first = 0;
        std::memcpy(&first, px, 4);
        CHECK(first == rgba(0, 0, level(1)));  // in the colours seen
        khdays_png_free(px);
    }
}

TEST(the_tile_grid_lies_over_the_export) {
    Assembly a;
    a.width = 16;
    a.height = 8;
    a.spots = {{0, 0, 0}, {8, 0, 1}};
    const std::uint32_t grey = 0xff808080u, red = 0xff0000ffu;
    const auto at = [](const Bytes& p, int w, int x, int y) {
        std::uint32_t v = 0;
        std::memcpy(
            &v, &p[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4], 4);
        return v;
    };
    // at 1x: one-pixel borders, the marked tile in its colour, the insides clear
    const Bytes one = render_tile_grid(a, 1, grey, {{{1}, red}});
    CHECK(one.size() == static_cast<std::size_t>(16) * 8 * 4);
    CHECK(at(one, 16, 0, 0) == grey && at(one, 16, 7, 4) == grey);
    CHECK(at(one, 16, 3, 3) == 0);
    CHECK(at(one, 16, 8, 0) == red && at(one, 16, 15, 7) == red);
    CHECK(at(one, 16, 11, 4) == 0);
    // at 8x the same places, two pixels thick
    const Bytes eight = render_tile_grid(a, 8, grey, {});
    CHECK(at(eight, 128, 1, 1) == grey && at(eight, 128, 2, 2) == 0);
    CHECK(at(eight, 128, 64, 30) == grey && at(eight, 128, 127, 63) == grey);
}
