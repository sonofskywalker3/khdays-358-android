#include "core/sheets/sheets.h"

#include "core/files/file_write.h"
#include "core/models/game_model.h"
#include "core/files/paths.h"

#include "khdays/assets/cell.h"

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <unordered_map>

namespace studio {

namespace {

namespace assets = khdays::assets;

std::uint32_t le16(const std::vector<std::uint8_t>& d, std::size_t at) {
    return at + 2 <= d.size() ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8) : 0;
}
std::uint32_t le32(const std::vector<std::uint8_t>& d, std::size_t at) {
    return at + 4 <= d.size() ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 |
                                                           static_cast<std::uint32_t>(d[at + 3]) << 24)
                              : 0;
}

// A Nitro resource with `magic` at `at`: its byte order mark and a size that fits.
bool nitro_at(const std::vector<std::uint8_t>& d, std::size_t at, const char* magic) {
    return at + 0x10 <= d.size() && std::memcmp(&d[at], magic, 4) == 0 && le16(d, at + 4) == 0xfeff &&
           le32(d, at + 8) <= d.size() - at;
}

std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return static_cast<std::uint32_t>(r) | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(b) << 16 |
           static_cast<std::uint32_t>(a) << 24;
}

// The palette a picture is drawn with: the chosen one, or greys.
assets::Palette2D effective(const SheetSource& s, const assets::Palette2D& p) {
    if (!p.colors.empty())
        return p;
    assets::Palette2D greys;
    const int n = s.bpp() == 8 ? 256 : 16;
    greys.colors_per_palette = n;
    for (int i = 0; i < n; ++i) {
        const auto v = static_cast<std::uint8_t>(i * 255 / (n - 1));
        greys.colors.push_back({v, v, v, 255});
    }
    return greys;
}

// The colour of index `i` of row `row`: a 256-colour sheet whose palette has
// no such row takes the first, as the DS does without extended palettes; a
// 16-colour row past the palette is black (assets/src/graphics2d.cpp
// palette_color).
std::array<std::uint8_t, 4> colour(const SheetSource& s, const assets::Palette2D& p, int row, int i) {
    const int per = s.bpp() == 8 ? 256 : 16;
    std::size_t at = static_cast<std::size_t>(row) * static_cast<std::size_t>(per) + static_cast<std::size_t>(i);
    if (per == 256 && at >= p.colors.size())
        at = static_cast<std::size_t>(i);
    return at < p.colors.size() ? p.colors[at] : std::array<std::uint8_t, 4>{0, 0, 0, 255};
}

// Where a placed pixel at (X, Y) of a picture at scale k lies in a sheet at scale k.
std::size_t sheet_index(const SheetSource& s, const Place& pl, int X, int Y, int k, int sheet_width) {
    const int tx = pl.tile % s.columns, ty = pl.tile / s.columns;
    const int sx = (tx * 8 + pl.x) * k + (pl.flip_h ? k - 1 - X % k : X % k);
    const int sy = (ty * 8 + pl.y) * k + (pl.flip_v ? k - 1 - Y % k : Y % k);
    return static_cast<std::size_t>(sy) * static_cast<std::size_t>(sheet_width) + static_cast<std::size_t>(sx);
}

void place_tile(Assembly& a, int x0, int y0, int tile, int row, bool fh, bool fv, bool front_wins) {
    for (int py = 0; py < 8; ++py) {
        for (int px = 0; px < 8; ++px) {
            const int x = x0 + px, y = y0 + py;
            if (x < 0 || y < 0 || x >= a.width || y >= a.height)
                continue;
            Place& p =
                a.places[static_cast<std::size_t>(y) * static_cast<std::size_t>(a.width) + static_cast<std::size_t>(x)];
            if (p.tile >= 0) {
                ++a.overlaps;
                if (front_wins)
                    continue;
            }
            p.tile = tile;
            p.x = static_cast<std::uint8_t>(fh ? 7 - px : px);
            p.y = static_cast<std::uint8_t>(fv ? 7 - py : py);
            p.row = static_cast<std::uint8_t>(row);
            p.flip_h = fh;
            p.flip_v = fv;
        }
    }
    const auto t = static_cast<std::size_t>(tile);
    a.spots.push_back({x0, y0, tile});
    ++a.uses[t];
    if (fh || fv)
        a.flipped[t] = true;
    if (a.row_of[t] < 0)
        a.row_of[t] = row;
    else if (a.row_of[t] != row)
        a.several_rows[t] = true;
}

// Where a screen's entry for the place (cx, cy) is: a screen more than 32
// tiles wide holds its places in strips 32 wide (the last narrower), each
// strip row by row down the whole height, as the DS's screen blocks are
// (`UI/sf/sf.p2#2`'s 352 x 192 screen 1: entry 32 is place (0, 1)).
std::size_t screen_entry(int width_tiles, int height_tiles, int cx, int cy) {
    const int strip = cx / 32, strip_width = std::min(32, width_tiles - strip * 32);
    return static_cast<std::size_t>(strip) * 32u * static_cast<std::size_t>(height_tiles) +
           static_cast<std::size_t>(cy) * static_cast<std::size_t>(strip_width) + static_cast<std::size_t>(cx % 32);
}

struct PieceTiles {
    int first = 0, wide = 0, high = 0;
};

// A cell piece's tiles in the sheet (1D mapping: its tiles in a row from its
// start, which is its tile number times the bank's boundary in bytes).
PieceTiles piece_tiles(const SheetSource& s, const assets::OamPiece& p, int boundary) {
    return {p.tile * boundary / s.tile_bytes, p.width / 8, p.height / 8};
}

}  // namespace

bool sheet_source(std::vector<std::uint8_t> data, const std::string& label, int number, SheetSource* out,
                  std::string* why) {
    SheetSource s;
    s.label = label;
    s.number = number;
    int found = 0;
    bool have = false;
    for (std::size_t at = 0; at + 0x18 <= data.size(); at += 4) {
        // names.cpp names_of / read_sheet, lines 212-238
        if (std::memcmp(&data[at], "RGCN", 4) == 0 && le32(data, at + 8) <= data.size() - at) {
            if (at + 0x30 > data.size() || le16(data, at + 4) != 0xfeff ||
                std::memcmp(&data[at + 0x10], "RAHC", 4) != 0) {
                continue;
            }
            const std::uint32_t bytes = le32(data, at + 0x28);
            const std::uint32_t start = static_cast<std::uint32_t>(at + 0x18 + le32(data, at + 0x2c));
            const std::uint32_t tile_bytes = le32(data, at + 0x1c) == 4 ? 64 : 32;
            if (bytes < tile_bytes || start > data.size() || bytes > data.size() - start)
                continue;
            ++s.sheets_in_data;
            if (found++ != number)
                continue;
            have = true;
            s.tiles_at = start;
            s.tile_bytes = static_cast<int>(tile_bytes);
            s.tiles = static_cast<int>(bytes / tile_bytes);
            const std::uint32_t h = le16(data, at + 0x18), w = le16(data, at + 0x1a);
            s.shaped = w != 0xffff && h != 0xffff && w > 0 && w <= 128 && w * h == bytes / tile_bytes;
            s.columns = static_cast<int>(s.shaped ? w : s.tiles < 16 ? static_cast<std::uint32_t>(s.tiles) : 16u);
        }
        if (nitro_at(data, at, "RCSN"))
            s.screens.push_back({at, le32(data, at + 8)});
        if (nitro_at(data, at, "RECN"))
            s.cell_banks.push_back({at, le32(data, at + 8)});
        if (nitro_at(data, at, "RLCN"))
            s.palettes.push_back({at, le32(data, at + 8)});
    }
    if (!have) {
        if (why != nullptr)
            *why = "no sheet " + std::to_string(number) + " in " + label;
        return false;
    }
    s.data = std::move(data);
    *out = std::move(s);
    return true;
}

namespace {

// The game files a localized one stands beside: its name without a language
// (`res_es.p2` -> `res.p2`, `res_i18n.p2` -> `res.p2`, `bg_de.p2d.z` ->
// `bg.p2d.z`) and its folder without one (`UI/btl/es/main.p2` ->
// `UI/btl/main.p2`), those that exist.
std::vector<std::string> base_files(const fs::path& nitrofs, const std::string& file) {
    std::vector<std::string> out;
    const auto slash = file.rfind('/');
    const std::string dir = slash == std::string::npos ? std::string() : file.substr(0, slash);
    const std::string name = slash == std::string::npos ? file : file.substr(slash + 1);
    const auto dot = name.find('.');
    const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
    const std::string rest = dot == std::string::npos ? std::string() : name.substr(dot);
    auto add = [&](const std::string& path) {
        std::error_code ec;
        if (path != file && fs::is_regular_file(nitrofs / path_u8(path), ec) &&
            std::find(out.begin(), out.end(), path) == out.end()) {
            out.push_back(path);
        }
    };
    for (const char* language : {"_de", "_en", "_es", "_fr", "_it", "_i18n"}) {
        const std::string suffix = language;
        if (stem.size() > suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
            add((dir.empty() ? "" : dir + "/") + stem.substr(0, stem.size() - suffix.size()) + rest);
        }
    }
    const auto up = dir.rfind('/');
    const std::string last = up == std::string::npos ? dir : dir.substr(up + 1);
    for (const char* language : {"de", "en", "es", "fr", "it"}) {
        if (last == language)
            add((up == std::string::npos ? std::string() : dir.substr(0, up) + "/") + name);
    }
    return out;
}

int entry_of(const std::string& label) {
    const auto hash = label.rfind('#');
    return hash == std::string::npos ? -1 : std::atoi(label.c_str() + hash + 1);
}

// A copy of `from`'s bytes [r.at, r.at + r.size) at the end of the sheet's
// data, 4-aligned (the sheet's own tiles stay where they are).
Resource append_resource(SheetSource* s, const std::vector<std::uint8_t>& from, const Resource& r) {
    while (s->data.size() % 4 != 0)
        s->data.push_back(0);
    const Resource out{s->data.size(), r.size};
    s->data.insert(s->data.end(), from.begin() + static_cast<std::ptrdiff_t>(r.at),
                   from.begin() + static_cast<std::ptrdiff_t>(r.at + r.size));
    return out;
}

bool tile_drawn(const SheetSource& s, int t) {
    const std::uint8_t* p = &s.data[s.tiles_at + static_cast<std::size_t>(t) * static_cast<std::size_t>(s.tile_bytes)];
    return std::any_of(p, p + s.tile_bytes, [](std::uint8_t b) { return b != 0; });
}

// How many drawn tiles (a pixel not index 0) two sheets of the same size
// hold byte for byte the same.
std::size_t same_tiles(const SheetSource& a, const SheetSource& b) {
    std::size_t same = 0;
    const auto n = static_cast<std::size_t>(a.tile_bytes);
    for (int t = 0; t < a.tiles && t < b.tiles; ++t) {
        same += tile_drawn(a, t) && std::memcmp(&a.data[a.tiles_at + static_cast<std::size_t>(t) * n],
                                                &b.data[b.tiles_at + static_cast<std::size_t>(t) * n], n) == 0
                    ? 1
                    : 0;
    }
    return same;
}

}  // namespace

// A sheet with no screens or cells of its own in its data borrows its twin's (the same depth, tiles and
// columns, the most drawn tiles byte for byte its own), or for a file of its own its folder's screen
// and cell files, the name most like its own first (docs/STUDIO.md, "2D sheets").
static void borrow_arrangements(const fs::path& nitrofs, SheetSource* s) {
    if (!s->screens.empty() || !s->cell_banks.empty())
        return;
    const auto hash = s->label.rfind('#');
    const std::string file = hash == std::string::npos ? s->label : s->label.substr(0, hash);
    const int entry = entry_of(s->label);
    std::size_t drawn = 0;
    for (int t = 0; t < s->tiles; ++t)
        drawn += tile_drawn(*s, t) ? 1 : 0;
    SheetSource twin;
    std::size_t best = 0;
    std::vector<std::string> looked;
    auto search = [&](const std::string& path, int around, bool only_same) {
        auto all = game_file_data(nitrofs, path);
        std::stable_sort(all.begin(), all.end(), [&](const GameData& a, const GameData& b) {
            return std::abs(entry_of(a.label) - around) < std::abs(entry_of(b.label) - around);
        });
        for (const auto& d : all) {
            if (d.label == s->label || (only_same && entry_of(d.label) != around))
                continue;
            if (std::find(looked.begin(), looked.end(), d.label) != looked.end())
                continue;
            looked.push_back(d.label);
            for (int n = 0;; ++n) {
                SheetSource t;
                if (!sheet_source(d.bytes, d.label, n, &t, nullptr))
                    break;
                if (t.screens.empty() && t.cell_banks.empty())
                    break;  // the data's, whichever sheet
                if (t.tile_bytes != s->tile_bytes || t.tiles != s->tiles || t.columns != s->columns)
                    continue;
                const std::size_t same = same_tiles(*s, t);
                if (same > best && same * 8 >= drawn)
                    twin = std::move(t), best = same;
            }
        }
    };
    const auto bases = base_files(nitrofs, file);
    for (const auto& b : bases)
        search(b, entry, true);
    if (hash != std::string::npos)
        search(file, entry, false);
    for (const auto& b : bases)
        search(b, entry, false);
    if (best > 0) {
        s->borrowed = twin.label + "/chr/" + std::to_string(twin.number);
        for (const auto& r : twin.screens)
            s->screens.push_back(append_resource(s, twin.data, r));
        for (const auto& r : twin.cell_banks)
            s->cell_banks.push_back(append_resource(s, twin.data, r));
        const auto twin_fits = fitting_arrangements(twin);
        const int drawn_with = twin_fits.empty() ? -1 : twin_fits.front().palette;
        std::vector<std::size_t> order;
        if (drawn_with >= 0)
            order.push_back(static_cast<std::size_t>(drawn_with));
        for (std::size_t k = 0; k < twin.palettes.size(); ++k) {
            if (static_cast<int>(k) != drawn_with)
                order.push_back(k);
        }
        for (const std::size_t k : order) {
            const Resource& r = twin.palettes[k];
            SheetSource::NearbyPalette p;
            p.from = twin.label + (twin.palettes.size() > 1 ? " palette " + std::to_string(k) : std::string());
            p.bytes.assign(twin.data.begin() + static_cast<std::ptrdiff_t>(r.at),
                           twin.data.begin() + static_cast<std::ptrdiff_t>(r.at + r.size));
            s->nearby.push_back(std::move(p));
        }
        return;
    }
    if (hash != std::string::npos)
        return;
    const auto slash = file.rfind('/');
    const std::string dir = slash == std::string::npos ? std::string() : file.substr(0, slash);
    const std::string name = slash == std::string::npos ? file : file.substr(slash + 1);
    std::vector<std::pair<std::size_t, std::string>> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(nitrofs / path_u8(dir), ec)) {
        const std::string other = u8(e.path().filename());
        const std::string low = lower(other);
        if (low.find(".nscr") == std::string::npos && low.find(".ncer") == std::string::npos)
            continue;
        std::size_t common = 0;
        while (common < other.size() && common < name.size() && other[common] == name[common])
            ++common;
        files.emplace_back(common, (dir.empty() ? "" : dir + "/") + other);
    }
    std::stable_sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    for (const auto& f : files) {
        for (const auto& d : game_file_data(nitrofs, f.second)) {
            const std::size_t had = s->screens.size() + s->cell_banks.size();
            for (std::size_t at = 0; at + 0x18 <= d.bytes.size(); at += 4) {
                const Resource r{at, le32(d.bytes, at + 8)};
                if (nitro_at(d.bytes, at, "RCSN"))
                    s->screens.push_back(append_resource(s, d.bytes, r));
                if (nitro_at(d.bytes, at, "RECN"))
                    s->cell_banks.push_back(append_resource(s, d.bytes, r));
            }
            if (s->screens.size() + s->cell_banks.size() > had)
                s->borrowed += (s->borrowed.empty() ? "" : ", ") + d.label;
        }
    }
}

static void gather_nearby(const fs::path& nitrofs, SheetSource* s) {
    const auto hash = s->label.rfind('#');
    const std::string file = hash == std::string::npos ? s->label : s->label.substr(0, hash);
    const int entry = entry_of(s->label);
    std::vector<std::string> seen{s->label};
    // a twin's data: its palettes are in already
    if (const auto chr = s->borrowed.rfind("/chr/"); chr != std::string::npos)
        seen.push_back(s->borrowed.substr(0, chr));
    auto take = [&](const GameData& d) {
        if (std::find(seen.begin(), seen.end(), d.label) != seen.end())
            return;
        seen.push_back(d.label);
        std::vector<Resource> found;
        for (std::size_t at = 0; at + 0x18 <= d.bytes.size(); at += 4) {
            if (nitro_at(d.bytes, at, "RLCN"))
                found.push_back({at, le32(d.bytes, at + 8)});
        }
        for (std::size_t k = 0; k < found.size(); ++k) {
            SheetSource::NearbyPalette p;
            p.from = d.label + (found.size() > 1 ? " palette " + std::to_string(k) : std::string());
            p.bytes.assign(d.bytes.begin() + static_cast<std::ptrdiff_t>(found[k].at),
                           d.bytes.begin() + static_cast<std::ptrdiff_t>(found[k].at + found[k].size));
            s->nearby.push_back(std::move(p));
        }
    };
    // a file's entries, the one numbered `around` first, then by distance from it
    auto by_distance = [&](const std::string& path, int around, bool only_same) {
        auto all = game_file_data(nitrofs, path);
        std::stable_sort(all.begin(), all.end(), [&](const GameData& a, const GameData& b) {
            return std::abs(entry_of(a.label) - around) < std::abs(entry_of(b.label) - around);
        });
        for (const auto& d : all) {
            if (only_same && entry_of(d.label) != around)
                continue;
            take(d);
        }
    };
    const auto bases = base_files(nitrofs, file);
    for (const auto& b : bases)
        by_distance(b, entry, true);
    by_distance(file, entry, false);
    for (const auto& b : bases)
        by_distance(b, entry, false);
    // the folder's palette files, the name most like the sheet's first
    const auto slash = file.rfind('/');
    const std::string dir = slash == std::string::npos ? std::string() : file.substr(0, slash);
    const std::string name = slash == std::string::npos ? file : file.substr(slash + 1);
    std::vector<std::pair<std::size_t, std::string>> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(nitrofs / path_u8(dir), ec)) {
        const std::string other = u8(e.path().filename());
        if (lower(other).find(".nclr") == std::string::npos)
            continue;
        std::size_t common = 0;
        while (common < other.size() && common < name.size() && other[common] == name[common])
            ++common;
        files.emplace_back(common, (dir.empty() ? "" : dir + "/") + other);
    }
    std::stable_sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    for (const auto& f : files) {
        for (const auto& d : game_file_data(nitrofs, f.second))
            take(d);
    }
    // still none: the folder's other files, and a language folder's parent's
    // (`UI/btl/es/bl000.NCGR.z`: `UI/btl/es/main.p2`, `UI/btl/main.p2`, ...)
    if (!s->nearby.empty() || !s->palettes.empty())
        return;
    std::vector<std::string> dirs{dir};
    const auto up = dir.rfind('/');
    const std::string last = up == std::string::npos ? dir : dir.substr(up + 1);
    for (const char* language : {"de", "en", "es", "fr", "it"}) {
        if (last == language && up != std::string::npos)
            dirs.push_back(dir.substr(0, up));
    }
    for (const auto& d : dirs) {
        std::vector<std::string> others;
        for (const auto& e : fs::directory_iterator(nitrofs / path_u8(d), ec)) {
            std::error_code type_ec;
            if (!e.is_regular_file(type_ec))
                continue;
            const std::string path = d + "/" + u8(e.path().filename());
            if (path != file)
                others.push_back(path);
        }
        std::sort(others.begin(), others.end());
        for (const auto& o : others) {
            for (const auto& g : game_file_data(nitrofs, o))
                take(g);
        }
    }
}

bool load_sheet_source(const fs::path& nitrofs, const std::string& key, SheetSource* out, std::string* why) {
    const auto chr = key.rfind("/chr/");
    if (chr == std::string::npos) {
        if (why != nullptr)
            *why = "not a sheet's name: " + key;
        return false;
    }
    const std::string label = key.substr(0, chr);
    std::vector<std::uint8_t> data = game_label_data(nitrofs, label);
    if (data.empty()) {
        if (why != nullptr)
            *why = "no data for " + label;
        return false;
    }
    if (!sheet_source(std::move(data), label, std::atoi(key.c_str() + chr + 5), out, why))
        return false;
    borrow_arrangements(nitrofs, out);
    gather_nearby(nitrofs, out);
    return true;
}

bool take_seen_colours(const fs::path& dump, const std::string& key, SheetSource* s) {
    if (dump.empty() || s->tiles <= 0 || s->seen_palette >= 0)
        return false;
    const fs::path file = dump / path_u8(key + ".colours");
    std::ifstream in(file);
    if (!in)
        return false;
    // a row's colours: 16, or 256 for a sheet of 8-bit tiles
    const std::size_t per = s->bpp() == 8 ? 256 : 16;
    std::map<int, std::vector<std::uint16_t>> rows;
    std::vector<int> tile_rows(static_cast<std::size_t>(s->tiles), -1);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string first;
        words >> first;
        if (first == "row") {
            // `row <r> <its colours, BGR555 in hex>`
            int r = -1;
            words >> r;
            std::vector<std::uint16_t> c;
            unsigned v = 0;
            while (words >> std::hex >> v)
                c.push_back(static_cast<std::uint16_t>(v & 0x7fffu));
            // a row of the game (0-15), or one of its own for colours the same row had for other
            // tiles (from 17: 16 is the greys of the tiles never seen)
            if (r >= 0 && r != kUnseenRow && c.size() == per)
                rows[r] = std::move(c);
            continue;
        }
        // `<first>-<last> <row>`
        const auto dash = first.find('-');
        if (dash == std::string::npos || first.empty() || first[0] == '#')
            continue;
        const long from = std::strtol(first.c_str(), nullptr, 10),
                   to = std::strtol(first.c_str() + dash + 1, nullptr, 10);
        int r = -1;
        words >> r;
        if (!words || from < 0 || to < from || r < 0 || r == kUnseenRow)
            continue;
        for (long t = from; t <= to && t < s->tiles; ++t)
            tile_rows[static_cast<std::size_t>(t)] = r;
    }
    // the tiles seen in a row whose colours are known
    int seen_tiles = 0;
    for (auto& r : tile_rows) {
        if (r >= 0 && rows.count(r) == 0)
            r = -1;
        if (r >= 0)
            ++seen_tiles;
    }
    if (seen_tiles == 0)
        return false;
    // the palette it would be coloured with, the rows seen put in
    Arrangement base;
    const auto fits = fitting_arrangements(*s);
    base.palette = fits.empty() ? -1 : fits.front().palette;
    assets::Palette2D p = arrangement_palette(*s, base);
    p.colors_per_palette = static_cast<int>(per);
    // the sheet's 16 rows, after them a row of greys for the tiles never seen
    // (no colours of the game guessed for them), then the rows of their own
    p.colors.resize(16 * per, {0, 0, 0, 255});
    for (std::size_t i = 0; i < per; ++i) {
        const auto grey = static_cast<std::uint8_t>(i * 255 / (per - 1));
        p.colors.push_back({grey, grey, grey, 255});
    }
    const int last = rows.empty() ? 0 : rows.rbegin()->first;
    if (last > kUnseenRow)
        p.colors.resize(static_cast<std::size_t>(last + 1) * per, {0, 0, 0, 255});
    for (const auto& [r, c] : rows) {
        for (std::size_t k = 0; k < per; ++k) {
            const auto r5 = static_cast<std::uint8_t>(c[k] & 0x1fu),
                       g5 = static_cast<std::uint8_t>((c[k] >> 5) & 0x1fu),
                       b5 = static_cast<std::uint8_t>((c[k] >> 10) & 0x1fu);
            p.colors[static_cast<std::size_t>(r) * per + k] = {static_cast<std::uint8_t>(r5 << 3 | r5 >> 2),
                                                               static_cast<std::uint8_t>(g5 << 3 | g5 >> 2),
                                                               static_cast<std::uint8_t>(b5 << 3 | b5 >> 2), 255};
        }
    }
    SheetSource::NearbyPalette seen;
    seen.from = u8(file);
    seen.colours = std::move(p);
    s->nearby.push_back(std::move(seen));
    s->seen_palette = palette_count(*s) - 1;
    s->seen_rows = std::move(tile_rows);
    s->seen_row = kUnseenRow;
    // those never seen that draw something (an empty tile shows nothing in any row)
    s->unseen_tiles = 0;
    for (int t = 0; t < s->tiles; ++t) {
        if (s->seen_rows[static_cast<std::size_t>(t)] < 0 && tile_draws(*s, t))
            ++s->unseen_tiles;
    }
    return true;
}

bool tile_draws(const SheetSource& s, int tile) {
    const std::size_t bytes = static_cast<std::size_t>(s.tile_bytes),
                      at = s.tiles_at + static_cast<std::size_t>(tile) * bytes;
    if (tile < 0 || at + bytes > s.data.size())
        return false;
    return std::any_of(s.data.begin() + static_cast<std::ptrdiff_t>(at),
                       s.data.begin() + static_cast<std::ptrdiff_t>(at + bytes), [](std::uint8_t b) { return b != 0; });
}

int palette_count(const SheetSource& s) {
    return static_cast<int>(s.palettes.size() + s.nearby.size());
}

std::string palette_from(const SheetSource& s, int i) {
    if (i < static_cast<int>(s.palettes.size()) || i >= palette_count(s))
        return {};
    return s.nearby[static_cast<std::size_t>(i) - s.palettes.size()].from;
}

SheetPixels game_sheet_pixels(const SheetSource& s) {
    SheetPixels p;
    p.width = s.width();
    p.height = s.height();
    p.px.assign(static_cast<std::size_t>(p.width) * static_cast<std::size_t>(p.height), 0);
    for (int t = 0; t < s.tiles; ++t) {
        const std::uint8_t* src =
            &s.data[s.tiles_at + static_cast<std::size_t>(t) * static_cast<std::size_t>(s.tile_bytes)];
        const int x0 = t % s.columns * 8, y0 = t / s.columns * 8;
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t v =
                s.tile_bytes == 64 ? src[i] : static_cast<std::uint32_t>((src[i / 2] >> ((i & 1) * 4)) & 15);
            p.px[static_cast<std::size_t>(y0 + i / 8) * static_cast<std::size_t>(p.width) +
                 static_cast<std::size_t>(x0 + i % 8)] = v;
        }
    }
    return p;
}

bool read_sheet_png(const fs::path& png, const SheetSource& s, SheetPixels* out, std::string* why) {
    const std::string path = u8(png);
    const char* error = nullptr;
    int w = 0, h = 0;
    std::uint8_t* px = khdays_png_read_indexed(path.c_str(), &w, &h, nullptr, &error);
    const bool indexed = px != nullptr;
    if (px == nullptr && error != nullptr && std::strcmp(error, "not indexed") == 0)
        px = khdays_png_read(path.c_str(), &w, &h, &error);
    if (px == nullptr) {
        if (why != nullptr)
            *why = error != nullptr ? error : "unreadable";
        return false;
    }
    // tiles2d.c lines 98-104: a whole multiple wide, at least as many times high
    const int scale = w / s.width();
    if (scale < 1 || scale > 255 || w != scale * s.width() || h < scale * s.height()) {
        khdays_png_free(px);
        if (why != nullptr)
            *why = "not a whole multiple of the sheet's size";
        return false;
    }
    SheetPixels p;
    p.scale = scale;
    p.indexed = indexed;
    p.width = w;
    p.height = scale * s.height();
    p.px.resize(static_cast<std::size_t>(p.width) * static_cast<std::size_t>(p.height));
    for (std::size_t i = 0; i < p.px.size(); ++i) {
        p.px[i] = indexed ? px[i] : rgba(px[4 * i], px[4 * i + 1], px[4 * i + 2], px[4 * i + 3]);
    }
    khdays_png_free(px);
    *out = std::move(p);
    return true;
}

std::string write_sheet_png(const fs::path& png, const SheetPixels& p,
                            const std::vector<std::array<std::uint8_t, 4>>& display) {
    const std::string path = u8(png);
    const char* error = nullptr;
    if (p.indexed) {
        std::vector<std::uint8_t> indices(p.px.size());
        for (std::size_t i = 0; i < p.px.size(); ++i)
            indices[i] = static_cast<std::uint8_t>(p.px[i]);
        const int n = std::clamp(static_cast<int>(display.size()), 2, 256);
        std::vector<std::uint8_t> rgb(static_cast<std::size_t>(n) * 3, 0);
        for (int i = 0; i < n && static_cast<std::size_t>(i) < display.size(); ++i) {
            for (int c = 0; c < 3; ++c)
                rgb[static_cast<std::size_t>(i) * 3 + static_cast<std::size_t>(c)] =
                    display[static_cast<std::size_t>(i)][static_cast<std::size_t>(c)];
        }
        error = khdays_png_write_indexed(path.c_str(), indices.data(), p.width, p.height, rgb.data(), n, 1);
    } else {
        std::vector<std::uint8_t> bytes(p.px.size() * 4);
        for (std::size_t i = 0; i < p.px.size(); ++i) {
            for (int c = 0; c < 4; ++c)
                bytes[4 * i + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(p.px[i] >> (8 * c));
        }
        error = khdays_png_write(path.c_str(), bytes.data(), p.width, p.height);
    }
    return error != nullptr ? std::string(error) : std::string();
}

std::vector<Arrangement> fitting_arrangements(const SheetSource& s) {
    std::vector<Arrangement> out;
    // of its own palettes, the one giving the sheet's indices the most
    // different colours (several may be black there: `UI/mlt/res.p2#0`'s
    // sheets are drawn only by the last of its four), the first on a tie;
    // those of its depth before those only holding enough colours for it (a
    // 256-colour sheet's palette may say 16 colours a row)
    int palette = -1, enough = -1, palette_lively = -1, enough_lively = -1;
    for (std::size_t i = 0; i < s.palettes.size(); ++i) {
        try {
            Arrangement probe;
            probe.palette = static_cast<int>(i);
            const auto p = arrangement_palette(s, probe);
            if (p.colors.size() < (s.bpp() == 8 ? 256u : 16u))
                continue;
            const int lively = liveliness(s, p);
            if ((p.colors_per_palette == 256) == (s.bpp() == 8)) {
                if (lively > palette_lively)
                    palette = static_cast<int>(i), palette_lively = lively;
            } else if (lively > enough_lively) {
                enough = static_cast<int>(i), enough_lively = lively;
            }
        } catch (const std::exception&) {
        }
    }
    if (palette < 0)
        palette = enough;
    // none of its own: the first nearby one giving at least half the colours of the liveliest
    if (palette < 0 && !s.nearby.empty()) {
        std::vector<int> lively(s.nearby.size(), 0);
        int most = 0;
        for (std::size_t i = 0; i < s.nearby.size(); ++i) {
            Arrangement probe;
            probe.palette = static_cast<int>(s.palettes.size() + i);
            const auto p = arrangement_palette(s, probe);
            if (p.colors.size() < (s.bpp() == 8 ? 256u : 16u))
                continue;
            lively[i] = liveliness(s, p);
            most = std::max(most, lively[i]);
        }
        for (std::size_t i = 0; i < s.nearby.size() && palette < 0; ++i) {
            if (lively[i] > 0 && lively[i] * 2 >= most)
                palette = static_cast<int>(s.palettes.size() + i);
        }
    }
    // data with as many palettes as sheets: each sheet's own
    if (s.sheets_in_data > 1 && static_cast<int>(s.palettes.size()) == s.sheets_in_data)
        palette = s.number;
    // the colours the game was seen drawing it with, above all
    if (s.seen_palette >= 0)
        palette = s.seen_palette;
    for (std::size_t i = 0; i < s.screens.size(); ++i) {
        const auto& r = s.screens[i];
        // the screen's colour mode (+0x1c: 0 16 colours, 1 256) is the sheet's depth
        if ((le16(s.data, r.at + 0x1c) == 1) != (s.bpp() == 8))
            continue;
        try {
            const auto map = assets::decode_nscr(&s.data[r.at], r.size);
            bool fits = !map.cells.empty();
            for (const auto& c : map.cells)
                fits = fits && c.tile < s.tiles;
            if (fits)
                out.push_back({false, static_cast<int>(i), palette});
        } catch (const std::exception&) {
        }
    }
    for (std::size_t i = 0; i < s.cell_banks.size(); ++i) {
        const auto& r = s.cell_banks[i];
        try {
            const auto bank = assets::decode_ncer(&s.data[r.at], r.size);
            bool fits = !bank.cells.empty();
            for (const auto& cell : bank.cells) {
                for (const auto& piece : cell.pieces) {
                    const PieceTiles t = piece_tiles(s, piece, bank.tile_boundary);
                    fits = fits && t.first + t.wide * t.high <= s.tiles;
                }
            }
            if (fits)
                out.push_back({true, static_cast<int>(i), palette});
        } catch (const std::exception&) {
        }
    }
    Arrangement whole;
    whole.palette = palette;
    whole.whole = true;
    if (s.seen_palette >= 0) {
        whole.row = s.seen_row;
        whole.rows = s.seen_rows;
    } else if (palette >= 0) {
        whole.row = liveliest_row(s, arrangement_palette(s, whole));
    }
    out.push_back(whole);
    return out;
}

int palette_rows(const SheetSource& s, const assets::Palette2D& palette) {
    const std::size_t per = s.bpp() == 8 ? 256 : 16;
    return std::max(1, static_cast<int>(palette.colors.size() / per));
}

namespace {

// Per row of `palette`, how many different colours it gives the indices the sheet uses.
std::vector<int> row_colours(const SheetSource& s, const assets::Palette2D& palette) {
    std::vector<int> out;
    if (palette.colors.empty())
        return out;
    const SheetPixels p = game_sheet_pixels(s);
    std::vector<bool> used(s.bpp() == 8 ? 256 : 16, false);
    for (const std::uint32_t v : p.px) {
        if (v != 0 && v < used.size())
            used[v] = true;
    }
    for (int row = 0; row < palette_rows(s, palette); ++row) {
        std::vector<std::uint32_t> seen;
        for (std::size_t i = 1; i < used.size(); ++i) {
            if (!used[i])
                continue;
            const auto c = colour(s, palette, row, static_cast<int>(i));
            const std::uint32_t k = static_cast<std::uint32_t>(c[0] >> 3) | static_cast<std::uint32_t>(c[1] >> 3) << 5 |
                                    static_cast<std::uint32_t>(c[2] >> 3) << 10;
            if (std::find(seen.begin(), seen.end(), k) == seen.end())
                seen.push_back(k);
        }
        out.push_back(static_cast<int>(seen.size()));
    }
    return out;
}

}  // namespace

int liveliness(const SheetSource& s, const assets::Palette2D& palette) {
    const auto rows = row_colours(s, palette);
    return rows.empty() ? 0 : *std::max_element(rows.begin(), rows.end());
}

int liveliest_row(const SheetSource& s, const assets::Palette2D& palette) {
    const auto rows = row_colours(s, palette);
    return rows.empty() ? 0 : static_cast<int>(std::max_element(rows.begin(), rows.end()) - rows.begin());
}

bool assemble(const SheetSource& s, const Arrangement& arr, Assembly* out, std::string* why) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    Assembly a;
    auto start = [&](int w, int h) {
        a.width = w;
        a.height = h;
        a.places.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), Place{});
        a.uses.assign(static_cast<std::size_t>(s.tiles), 0);
        a.flipped.assign(static_cast<std::size_t>(s.tiles), false);
        a.row_of.assign(static_cast<std::size_t>(s.tiles), -1);
        a.several_rows.assign(static_cast<std::size_t>(s.tiles), false);
    };
    try {
        if (arr.whole) {
            // the sheet as it is: its tiles in its rows, one palette row for all
            start(s.width(), s.height());
            for (int t = 0; t < s.tiles; ++t) {
                const int own =
                    static_cast<std::size_t>(t) < arr.rows.size() ? arr.rows[static_cast<std::size_t>(t)] : -1;
                place_tile(a, t % s.columns * 8, t / s.columns * 8, t, own >= 0 ? own : arr.row, false, false, true);
            }
        } else if (!arr.cells) {
            if (arr.resource < 0 || static_cast<std::size_t>(arr.resource) >= s.screens.size())
                return fail("no such screen");
            const auto& r = s.screens[static_cast<std::size_t>(arr.resource)];
            const auto map = assets::decode_nscr(&s.data[r.at], r.size);
            if (map.width_tiles <= 0 || map.height_tiles <= 0)
                return fail("an empty screen");
            start(map.width_tiles * 8, map.height_tiles * 8);
            for (int cy = 0; cy < map.height_tiles; ++cy) {
                for (int cx = 0; cx < map.width_tiles; ++cx) {
                    const std::size_t i = screen_entry(map.width_tiles, map.height_tiles, cx, cy);
                    if (i >= map.cells.size())
                        continue;
                    const auto& c = map.cells[i];
                    if (c.tile >= s.tiles)
                        continue;
                    place_tile(a, cx * 8, cy * 8, c.tile, c.palette, c.flip_h, c.flip_v, true);
                }
            }
        } else {
            if (arr.resource < 0 || static_cast<std::size_t>(arr.resource) >= s.cell_banks.size())
                return fail("no such cell bank");
            const auto& r = s.cell_banks[static_cast<std::size_t>(arr.resource)];
            const auto bank = assets::decode_ncer(&s.data[r.at], r.size);
            // each cell in its own box, side by side, rows no wider than 256 (or the widest cell)
            struct Box {
                int x0, y0, w, h, at_x, at_y;
            };
            std::vector<Box> boxes;
            int widest = 0;
            for (const auto& cell : bank.cells) {
                Box b{0, 0, 0, 0, 0, 0};
                bool first = true;
                int x1 = 0, y1 = 0;
                for (const auto& p : cell.pieces) {
                    if (first) {
                        b.x0 = p.x, b.y0 = p.y, x1 = p.x + p.width, y1 = p.y + p.height;
                        first = false;
                    } else {
                        b.x0 = std::min(b.x0, p.x), b.y0 = std::min(b.y0, p.y);
                        x1 = std::max(x1, p.x + p.width), y1 = std::max(y1, p.y + p.height);
                    }
                }
                b.w = x1 - b.x0;
                b.h = y1 - b.y0;
                widest = std::max(widest, b.w);
                boxes.push_back(b);
            }
            const int row_width = std::max(256, widest), gap = 4;
            int x = 0, y = 0, row_h = 0, total_w = 0;
            for (auto& b : boxes) {
                if (b.w == 0)
                    continue;
                if (x > 0 && x + b.w > row_width) {
                    x = 0;
                    y += row_h + gap;
                    row_h = 0;
                }
                b.at_x = x;
                b.at_y = y;
                x += b.w + gap;
                row_h = std::max(row_h, b.h);
                total_w = std::max(total_w, b.at_x + b.w);
            }
            if (total_w == 0)
                return fail("a cell bank with no pieces");
            start(total_w, y + row_h);
            for (std::size_t c = 0; c < bank.cells.size(); ++c) {
                const auto& cell = bank.cells[c];
                const Box& b = boxes[c];
                // the lower OAM number in front, as the DS draws them (GBATEK, OBJ priority)
                for (std::size_t k = 0; k < cell.pieces.size(); ++k) {
                    const auto& p = cell.pieces[k];
                    const PieceTiles t = piece_tiles(s, p, bank.tile_boundary);
                    for (int ty = 0; ty < t.high; ++ty) {
                        for (int tx = 0; tx < t.wide; ++tx) {
                            const int tile = t.first + ty * t.wide + tx;
                            if (tile < 0 || tile >= s.tiles)
                                continue;
                            // a flipped piece mirrors where each of its tiles goes, and the tile itself
                            const int ox = p.flip_h ? t.wide - 1 - tx : tx;
                            const int oy = p.flip_v ? t.high - 1 - ty : ty;
                            place_tile(a, b.at_x + p.x - b.x0 + ox * 8, b.at_y + p.y - b.y0 + oy * 8, tile, p.palette,
                                       p.flip_h, p.flip_v, true);
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    *out = std::move(a);
    return true;
}

bool assemble_stack(const SheetSource& s, const std::vector<Arrangement>& list, Assembly* out, std::vector<int>* tops,
                    std::string* why) {
    if (list.empty()) {
        if (why != nullptr)
            *why = "nothing chosen";
        return false;
    }
    std::vector<Assembly> parts(list.size());
    int width = 0, height = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!assemble(s, list[i], &parts[i], why))
            return false;
        width = std::max(width, parts[i].width);
        height += parts[i].height + (i > 0 ? 8 : 0);
    }
    Assembly a;
    a.width = width;
    a.height = height;
    a.places.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), Place{});
    a.uses.assign(static_cast<std::size_t>(s.tiles), 0);
    a.flipped.assign(static_cast<std::size_t>(s.tiles), false);
    a.row_of.assign(static_cast<std::size_t>(s.tiles), -1);
    a.several_rows.assign(static_cast<std::size_t>(s.tiles), false);
    std::vector<int> starts;
    int y0 = 0;
    for (const Assembly& p : parts) {
        starts.push_back(y0);
        for (int y = 0; y < p.height; ++y) {
            std::copy(p.places.begin() + static_cast<std::ptrdiff_t>(y) * p.width,
                      p.places.begin() + static_cast<std::ptrdiff_t>(y + 1) * p.width,
                      a.places.begin() + static_cast<std::ptrdiff_t>(y0 + y) * width);
        }
        for (std::size_t t = 0; t < a.uses.size(); ++t) {
            a.uses[t] += p.uses[t];
            a.flipped[t] = a.flipped[t] || p.flipped[t];
            a.several_rows[t] = a.several_rows[t] || p.several_rows[t];
            if (p.row_of[t] >= 0) {
                if (a.row_of[t] < 0)
                    a.row_of[t] = p.row_of[t];
                else if (a.row_of[t] != p.row_of[t])
                    a.several_rows[t] = true;
            }
        }
        for (const auto& spot : p.spots)
            a.spots.push_back({spot.x, spot.y + y0, spot.tile});
        a.overlaps += p.overlaps;
        y0 += p.height + 8;
    }
    if (tops != nullptr)
        *tops = std::move(starts);
    *out = std::move(a);
    return true;
}

assets::Palette2D arrangement_palette(const SheetSource& s, const Arrangement& a) {
    if (a.palette < 0 || a.palette >= palette_count(s))
        return {};
    try {
        if (static_cast<std::size_t>(a.palette) >= s.palettes.size()) {
            const auto& n = s.nearby[static_cast<std::size_t>(a.palette) - s.palettes.size()];
            if (n.bytes.empty())
                return n.colours;
            return assets::decode_nclr(n.bytes.data(), n.bytes.size());
        }
        const auto& r = s.palettes[static_cast<std::size_t>(a.palette)];
        return assets::decode_nclr(&s.data[r.at], r.size);
    } catch (const std::exception&) {
        return {};
    }
}

float border_jump(const std::vector<std::uint8_t>& rgba, int width, int height) {
    double border = 0, inside = 0;
    long nb = 0, ni = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 1; x < width; ++x) {
            const std::uint8_t* a = &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                           static_cast<std::size_t>(x) - 1) *
                                          4];
            const std::uint8_t* b = a + 4;
            if (a[3] == 0 || b[3] == 0)
                continue;
            const int d = std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]);
            if (x % 8 == 0)
                border += d, ++nb;
            else
                inside += d, ++ni;
        }
    }
    if (nb == 0 || ni == 0)
        return -1.0f;
    return static_cast<float>((border / static_cast<double>(nb)) / std::max(inside / static_cast<double>(ni), 1e-3));
}

std::vector<ArrangementScore> score_arrangements(const SheetSource& s, const std::vector<Arrangement>& fits,
                                                 const SheetPixels& current) {
    const SheetPixels small = rescale(current, 1);
    // the sheet's drawn tiles: a pixel other than index 0
    std::vector<bool> drawn(static_cast<std::size_t>(s.tiles), false);
    int drawn_count = 0;
    for (int t = 0; t < s.tiles; ++t) {
        const std::uint8_t* src =
            &s.data[s.tiles_at + static_cast<std::size_t>(t) * static_cast<std::size_t>(s.tile_bytes)];
        drawn[static_cast<std::size_t>(t)] =
            std::any_of(src, src + s.tile_bytes, [](std::uint8_t b) { return b != 0; });
        drawn_count += drawn[static_cast<std::size_t>(t)] ? 1 : 0;
    }
    std::vector<ArrangementScore> out;
    for (const auto& f : fits) {
        ArrangementScore sc;
        sc.arrangement = f;
        Assembly one;
        if (assemble(s, f, &one, nullptr)) {
            sc.jump = border_jump(render_assembly(s, one, small, arrangement_palette(s, f)), one.width, one.height);
            for (std::size_t t = 0; t < drawn.size(); ++t) {
                if (drawn[t] && one.uses[t] > 0)
                    sc.tiles.push_back(static_cast<int>(t));
            }
            sc.shown = drawn_count > 0 ? static_cast<float>(sc.tiles.size()) / static_cast<float>(drawn_count) : 0.0f;
        }
        if (!f.whole && !f.cells && f.resource >= 0 && static_cast<std::size_t>(f.resource) < s.screens.size()) {
            try {
                const auto& r = s.screens[static_cast<std::size_t>(f.resource)];
                const auto map = assets::decode_nscr(&s.data[r.at], r.size);
                // an entry holds 10 bits of tile: past 1023 the count starts again from 0
                std::size_t in_order = 0, places = 0;
                std::array<std::size_t, 16> rows{};
                for (int cy = 0; cy < map.height_tiles; ++cy) {
                    for (int cx = 0; cx < map.width_tiles; ++cx) {
                        const std::size_t i = screen_entry(map.width_tiles, map.height_tiles, cx, cy);
                        if (i >= map.cells.size())
                            continue;
                        const auto& c = map.cells[i];
                        const int k = cy * map.width_tiles + cx;
                        in_order += c.tile == k % 1024 && !c.flip_h && !c.flip_v ? 1 : 0;
                        ++rows[static_cast<std::size_t>(c.palette & 15)];
                        ++places;
                    }
                }
                sc.in_order = places > 0 && in_order * 10 >= places * 9;
                sc.row = static_cast<int>(std::max_element(rows.begin(), rows.end()) - rows.begin());
                sc.as_sheet = sc.in_order && map.width_tiles == s.columns;
            } catch (const std::exception&) {
            }
        }
        out.push_back(sc);
    }
    return out;
}

Ranked rank_arrangements(const SheetSource& s, const std::vector<Arrangement>& fits, const SheetPixels& current) {
    auto scores = score_arrangements(s, fits, current);
    const auto whole = std::find_if(scores.begin(), scores.end(), [](const auto& sc) { return sc.arrangement.whole; });
    auto joined = [](const ArrangementScore& sc) { return sc.jump >= 0 && sc.jump <= 3.0f && !sc.tiles.empty(); };
    auto score = [](const ArrangementScore& sc) { return sc.shown / std::max(sc.jump, 1.0f); };
    // the candidates: the screens and cells that join up; a screen laying the sheet's tiles as the
    // sheet does and showing at least half of it stands for the sheet as it is
    std::vector<const ArrangementScore*> candidates;
    const ArrangementScore* standing = nullptr;
    for (const auto& sc : scores) {
        if (sc.arrangement.whole || !joined(sc) || (sc.in_order && !sc.as_sheet))
            continue;
        if (!sc.as_sheet || sc.shown < 0.5f)
            candidates.push_back(&sc);
        else if (standing == nullptr || sc.shown > standing->shown)
            standing = &sc;
    }
    if (standing != nullptr && whole != scores.end()) {
        Assembly by_screen;
        if (s.bpp() == 4 && whole->arrangement.rows.empty() &&
            assemble(s, standing->arrangement, &by_screen, nullptr)) {
            whole->arrangement.row = standing->row;
            whole->arrangement.rows = by_screen.row_of;
            Assembly one;
            if (assemble(s, whole->arrangement, &one, nullptr)) {
                whole->jump = border_jump(
                    render_assembly(s, one, rescale(current, 1), arrangement_palette(s, whole->arrangement)), one.width,
                    one.height);
            }
        }
        if (joined(*whole))
            candidates.push_back(&*whole);
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [&](const auto* a, const auto* b) { return score(*a) > score(*b); });
    // data with as many screens (cell banks) as sheets: the sheet's own first,
    // unless it draws far less of it than the best
    for (const bool cells : {true, false}) {
        const std::size_t count = cells ? s.cell_banks.size() : s.screens.size();
        if (s.sheets_in_data < 2 || static_cast<int>(count) != s.sheets_in_data || candidates.empty())
            continue;
        const auto own = std::find_if(candidates.begin(), candidates.end(), [&](const auto* c) {
            return !c->arrangement.whole && c->arrangement.cells == cells && c->arrangement.resource == s.number;
        });
        if (own != candidates.end() && score(**own) >= 0.8f * score(*candidates.front())) {
            std::rotate(candidates.begin(), own, own + 1);
        }
    }
    // shown together: the best, then each one whose tiles are mostly (three in
    // four) others -- the screens of a picture share out its sheet's tiles,
    // one that only fits by its number of tiles draws tiles already shown
    std::vector<const ArrangementScore*> together;
    std::vector<bool> covered(static_cast<std::size_t>(s.tiles), false);
    std::size_t covered_count = 0, drawn_count = 0;
    if (whole != scores.end())
        drawn_count = whole->tiles.size();
    for (const auto* c : candidates) {
        std::size_t again = 0;
        for (const int t : c->tiles)
            again += covered[static_cast<std::size_t>(t)] ? 1 : 0;
        if (!together.empty() && (c->arrangement.whole || again * 4 > c->tiles.size()))
            continue;
        together.push_back(c);
        for (const int t : c->tiles) {
            if (!covered[static_cast<std::size_t>(t)])
                covered[static_cast<std::size_t>(t)] = true, ++covered_count;
        }
        if (c->arrangement.whole)
            break;
    }
    // together they show under half the sheet and the sheet as it is joins up
    // better: the sheet as it is (with none joining up, the order below: a
    // picture with hard edges may jump more)
    if (!together.empty() && covered_count * 2 < drawn_count && whole != scores.end() && joined(*whole) &&
        whole->jump < together.front()->jump) {
        together.assign(1, &*whole);
    }
    Ranked out;
    for (const auto* c : together)
        out.order.push_back(c->arrangement);
    out.together = std::max<std::size_t>(1, together.size());
    // the rest: those that join up best, those that draw nothing last, the
    // sheet as it is after them (first when none draws anything)
    std::vector<const ArrangementScore*> rest;
    for (const auto& sc : scores) {
        if (std::find(together.begin(), together.end(), &sc) == together.end())
            rest.push_back(&sc);
    }
    bool any_drawn = false;
    for (const auto* r : rest)
        any_drawn = any_drawn || (!r->arrangement.whole && r->jump >= 0);
    auto key = [&](const ArrangementScore* r) {
        if (r->arrangement.whole)
            return any_drawn ? 1e30f : -1.0f;
        return r->jump < 0 ? 2e30f : r->jump;
    };
    std::stable_sort(rest.begin(), rest.end(), [&](const auto* a, const auto* b) { return key(a) < key(b); });
    for (const auto* r : rest)
        out.order.push_back(r->arrangement);
    return out;
}

SheetPixels rescale(const SheetPixels& p, int scale) {
    if (scale == p.scale)
        return p;
    SheetPixels o;
    o.scale = scale;
    o.indexed = p.indexed;
    o.width = p.width / p.scale * scale;
    o.height = p.height / p.scale * scale;
    o.px.resize(static_cast<std::size_t>(o.width) * static_cast<std::size_t>(o.height));
    for (int y = 0; y < o.height; ++y) {
        const int sy = y * p.scale / scale;
        for (int x = 0; x < o.width; ++x) {
            const int sx = x * p.scale / scale;
            o.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(o.width) + static_cast<std::size_t>(x)] =
                p.px[static_cast<std::size_t>(sy) * static_cast<std::size_t>(p.width) + static_cast<std::size_t>(sx)];
        }
    }
    return o;
}

std::vector<std::uint8_t> render_assembly(const SheetSource& s, const Assembly& a, const SheetPixels& p,
                                          const assets::Palette2D& chosen) {
    const assets::Palette2D palette = effective(s, chosen);
    const int k = p.scale;
    const int w = a.width * k, h = a.height * k;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    for (int Y = 0; Y < h; ++Y) {
        for (int X = 0; X < w; ++X) {
            const Place& pl = a.places[static_cast<std::size_t>(Y / k) * static_cast<std::size_t>(a.width) +
                                       static_cast<std::size_t>(X / k)];
            if (pl.tile < 0)
                continue;
            const std::uint32_t v = p.px[sheet_index(s, pl, X, Y, k, p.width)];
            std::uint8_t* o =
                &out[(static_cast<std::size_t>(Y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(X)) * 4];
            if (p.indexed) {
                if (v == 0)
                    continue;  // transparent
                const auto c = colour(s, palette, pl.row, static_cast<int>(v));
                o[0] = c[0], o[1] = c[1], o[2] = c[2], o[3] = 255;
            } else {
                for (int c = 0; c < 4; ++c)
                    o[c] = static_cast<std::uint8_t>(v >> (8 * c));
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> render_tile_grid(const Assembly& a, int scale, std::uint32_t grid,
                                           const std::vector<TileMark>& marks) {
    const int w = a.width * scale, h = a.height * scale, side = 8 * scale;
    const int line = std::max(1, scale / 4);  // thin, but seen at every size
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    const auto outline = [&](const Assembly::Spot& spot, std::uint32_t rgba) {
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                if (x >= line && x < side - line && y >= line && y < side - line)
                    continue;
                const int X = spot.x * scale + x, Y = spot.y * scale + y;
                if (X >= w || Y >= h)
                    continue;
                std::uint8_t* o =
                    &out[(static_cast<std::size_t>(Y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(X)) * 4];
                for (int c = 0; c < 4; ++c)
                    o[c] = static_cast<std::uint8_t>(rgba >> (8 * c));
            }
        }
    };
    for (const auto& spot : a.spots)
        outline(spot, grid);
    for (const auto& m : marks) {
        std::vector<bool> wanted;
        for (const int t : m.tiles) {
            if (t < 0)
                continue;
            if (static_cast<std::size_t>(t) >= wanted.size())
                wanted.resize(static_cast<std::size_t>(t) + 1);
            wanted[static_cast<std::size_t>(t)] = true;
        }
        for (const auto& spot : a.spots) {
            if (spot.tile >= 0 && static_cast<std::size_t>(spot.tile) < wanted.size() &&
                wanted[static_cast<std::size_t>(spot.tile)])
                outline(spot, m.rgba);
        }
    }
    return out;
}

bool take_back(const SheetSource& s, const Assembly& a, const SheetPixels& current, const assets::Palette2D& chosen,
               const std::vector<std::uint8_t>& edited, int w, int h, Colours colours, bool all_changed, TakeBack* out,
               std::string* why) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    if (a.width <= 0 || w % a.width != 0)
        return fail("the picture is not the assembled picture's size times a whole number");
    const int k = w / a.width;
    if (k < 1 || k > 255 || h != k * a.height)
        return fail("the picture is not the assembled picture's size times a whole number");
    if (edited.size() != static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4)
        return fail("the picture's pixels are missing");

    const assets::Palette2D palette = effective(s, chosen);
    const int per = s.bpp() == 8 ? 256 : 16;
    const SheetPixels base = rescale(current, k);
    const std::vector<std::uint8_t> before = render_assembly(s, a, base, chosen);

    // each palette colour (5 bits a channel, as the DS holds them) to its first index, per row
    std::map<int, std::unordered_map<std::uint32_t, int>> exact;
    auto key5 = [](const std::uint8_t* c) {
        return static_cast<std::uint32_t>(c[0] >> 3) | static_cast<std::uint32_t>(c[1] >> 3) << 5 |
               static_cast<std::uint32_t>(c[2] >> 3) << 10;
    };
    auto row_map = [&](int row) -> const std::unordered_map<std::uint32_t, int>& {
        auto it = exact.find(row);
        if (it != exact.end())
            return it->second;
        std::unordered_map<std::uint32_t, int> m;
        for (int i = per - 1; i >= 1; --i) {  // index 0 is transparent: never an opaque pixel's
            const auto c = colour(s, palette, row, i);
            m[key5(c.data())] = i;
        }
        return exact.emplace(row, std::move(m)).first->second;
    };
    auto nearest = [&](int row, const std::uint8_t* c) {
        int best = 1;
        long best_d = -1;
        for (int i = 1; i < per; ++i) {
            const auto p = colour(s, palette, row, i);
            long d = 0;
            for (int ch = 0; ch < 3; ++ch) {
                const long e = static_cast<long>(p[static_cast<std::size_t>(ch)]) - static_cast<long>(c[ch]);
                d += e * e;
            }
            if (best_d < 0 || d < best_d)
                best_d = d, best = i;
        }
        return best;
    };

    TakeBack t;
    t.scale = k;
    struct Change {
        std::size_t at;      // in the sheet
        std::uint32_t rgba;  // the picture's
        int row;
        int tile;
    };
    std::vector<Change> changes;
    for (int Y = 0; Y < h; ++Y) {
        for (int X = 0; X < w; ++X) {
            const std::size_t i =
                (static_cast<std::size_t>(Y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(X)) * 4;
            const bool differs = std::memcmp(&edited[i], &before[i], 4) != 0 &&
                                 !(edited[i + 3] < 128 && before[i + 3] < 128);  // transparent either way
            const Place& pl = a.places[static_cast<std::size_t>(Y / k) * static_cast<std::size_t>(a.width) +
                                       static_cast<std::size_t>(X / k)];
            if (pl.tile < 0) {
                if (differs)
                    ++t.outside;
                continue;
            }
            if (!differs && !all_changed)
                continue;
            if (differs)
                ++t.changed;
            const std::uint32_t c = edited[i + 3] < 128 ? 0u : rgba(edited[i], edited[i + 1], edited[i + 2], 255);
            if (c != 0 && row_map(pl.row).count(key5(&edited[i])) == 0)
                ++t.off_palette;
            changes.push_back({sheet_index(s, pl, X, Y, k, base.width), c, pl.row, pl.tile});
        }
    }

    // the sheet's form: the game's palettes when every change is one of them (or fitted), else its own colours
    const bool indexed =
        base.indexed && (colours == Colours::fit || (colours == Colours::automatic && t.off_palette == 0));
    SheetPixels sheet = base;
    if (base.indexed && !indexed) {
        // its own colours: each tile with the palette row its first place here has
        sheet.indexed = false;
        for (int tile = 0; tile < s.tiles; ++tile) {
            int row = a.row_of[static_cast<std::size_t>(tile)];
            bool any = false;
            const int tx = tile % s.columns * 8 * k, ty = tile / s.columns * 8 * k;
            for (int y = 0; y < 8 * k; ++y) {
                for (int x = 0; x < 8 * k; ++x) {
                    std::uint32_t& v =
                        sheet.px[static_cast<std::size_t>(ty + y) * static_cast<std::size_t>(sheet.width) +
                                 static_cast<std::size_t>(tx + x)];
                    if (v == 0)
                        continue;
                    any = true;
                    const auto c = colour(s, palette, row < 0 ? 0 : row, static_cast<int>(v));
                    v = rgba(c[0], c[1], c[2], 255);
                }
            }
            if (row < 0 && any)
                ++t.unplaced_coloured;
            if (a.several_rows[static_cast<std::size_t>(tile)])
                t.several_rows.push_back(tile);
        }
    }
    std::unordered_map<std::size_t, std::uint32_t> written;
    std::vector<bool> conflict(static_cast<std::size_t>(s.tiles), false),
        touched(static_cast<std::size_t>(s.tiles), false);
    for (const Change& ch : changes) {
        std::uint32_t v = ch.rgba;
        if (indexed && v != 0) {
            const std::uint8_t c[3] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
                                       static_cast<std::uint8_t>(v >> 16)};
            // the index there when it already has that colour (a palette may hold a colour twice, and
            // the tile may be drawn with other rows too); else the colour's first index, else the nearest
            const std::uint32_t was = base.px[ch.at];
            const auto now = colour(s, palette, ch.row, static_cast<int>(was));
            const auto& m = row_map(ch.row);
            const auto found = m.find(key5(c));
            v = was != 0 && key5(now.data()) == key5(c)
                    ? was
                    : static_cast<std::uint32_t>(found != m.end() ? found->second : nearest(ch.row, c));
        } else if (!indexed && v != 0 && colours == Colours::fit) {
            const std::uint8_t c[3] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
                                       static_cast<std::uint8_t>(v >> 16)};
            const auto p = colour(s, palette, ch.row, nearest(ch.row, c));
            v = rgba(p[0], p[1], p[2], 255);
        }
        const auto [it, inserted] = written.emplace(ch.at, v);
        if (!inserted) {
            if (it->second != v)
                conflict[static_cast<std::size_t>(ch.tile)] = true;
            continue;  // the first change there stays
        }
        sheet.px[ch.at] = v;
        touched[static_cast<std::size_t>(ch.tile)] = true;
    }
    for (int tile = 0; tile < s.tiles; ++tile) {
        const auto i = static_cast<std::size_t>(tile);
        if (conflict[i])
            t.conflicts.push_back(tile);
        if (!touched[i] || all_changed)
            continue;
        if (a.uses[i] > 1)
            t.repeated.push_back(tile);
        if (a.flipped[i])
            t.flipped.push_back(tile);
    }
    t.sheet = std::move(sheet);
    *out = std::move(t);
    return true;
}

SheetPixels current_sheet(const fs::path& mod_dir, const std::string& key, const SheetSource& s, fs::path* from) {
    const fs::path file = mod_dir / "graphics" / path_u8(key + ".png");
    std::error_code ec;
    SheetPixels p;
    if (!mod_dir.empty() && fs::is_regular_file(file, ec) && read_sheet_png(file, s, &p, nullptr)) {
        if (from != nullptr)
            *from = file;
        return p;
    }
    if (from != nullptr)
        from->clear();
    return game_sheet_pixels(s);
}

std::vector<std::array<std::uint8_t, 4>> display_colours(const SheetSource& s, const Assembly& a,
                                                         const assets::Palette2D& chosen) {
    const assets::Palette2D palette = effective(s, chosen);
    std::map<int, int> rows;
    for (const int r : a.row_of) {
        if (r >= 0)
            ++rows[r];
    }
    int row = 0, most = 0;
    for (const auto& [r, n] : rows) {
        if (n > most)
            row = r, most = n;
    }
    const int per = s.bpp() == 8 ? 256 : 16;
    std::vector<std::array<std::uint8_t, 4>> out;
    for (int i = 0; i < per; ++i)
        out.push_back(colour(s, palette, row, i));
    return out;
}

bool save_sheet(const fs::path& mod_dir, const std::string& key, const SheetSource& s, const SheetPixels& sheet,
                const std::vector<std::array<std::uint8_t, 4>>& display, fs::path* written, std::string* why) {
    const fs::path destination = mod_dir / "graphics" / path_u8(key + ".png");
    const WriteResult r = write_into_mod(
        mod_dir, destination, [&](const fs::path& temporary) { return write_sheet_png(temporary, sheet, display); },
        [&](const fs::path& temporary) -> std::string {
            SheetPixels back;
            std::string problem;
            if (!read_sheet_png(temporary, s, &back, &problem))
                return "the port would not draw it: " + problem;
            if (back.indexed != sheet.indexed || back.scale != sheet.scale || back.px != sheet.px) {
                return "it does not read back as written";
            }
            return {};
        });
    if (!r.ok) {
        if (why != nullptr)
            *why = r.why;
        return false;
    }
    if (written != nullptr)
        *written = destination;
    return true;
}

namespace {

fs::path tile_rows_file(const fs::path& mod_dir, const std::string& key) {
    return mod_dir / "studio" / "rows" / path_u8(key + ".txt");
}

}  // namespace

std::vector<int> read_tile_rows(const fs::path& mod_dir, const std::string& key, int tiles) {
    std::vector<int> rows;
    if (mod_dir.empty() || tiles <= 0)
        return rows;
    std::ifstream in(tile_rows_file(mod_dir, key));
    std::string line;
    while (std::getline(in, line)) {
        // `<first>-<last> <row>`
        char* at = line.data();
        char* end = nullptr;
        const long first = std::strtol(at, &end, 10);
        if (end == at || *end != '-')
            continue;
        at = end + 1;
        const long last = std::strtol(at, &end, 10);
        if (end == at)
            continue;
        at = end;
        const long row = std::strtol(at, &end, 10);
        if (end == at || first < 0 || last < first || row < 0 || row > 255)
            continue;
        if (rows.empty())
            rows.assign(static_cast<std::size_t>(tiles), -1);
        for (long t = first; t <= last && t < tiles; ++t)
            rows[static_cast<std::size_t>(t)] = static_cast<int>(row);
    }
    return rows;
}

std::string write_tile_rows(const fs::path& mod_dir, const std::string& key, const std::vector<int>& rows) {
    const fs::path file = tile_rows_file(mod_dir, key);
    std::string text;
    for (std::size_t t = 0; t < rows.size();) {
        std::size_t end = t;
        while (end + 1 < rows.size() && rows[end + 1] == rows[t])
            ++end;
        if (rows[t] >= 0)
            text += std::to_string(t) + "-" + std::to_string(end) + " " + std::to_string(rows[t]) + "\n";
        t = end + 1;
    }
    std::error_code ec;
    if (text.empty()) {
        fs::remove(file, ec);
        return {};
    }
    const WriteResult r = write_into_mod(
        mod_dir, file,
        [&](const fs::path& temporary) -> std::string {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << text;
            return out ? std::string() : std::string("not written");
        },
        [&](const fs::path& temporary) -> std::string {
            std::ifstream in(temporary, std::ios::binary);
            const std::string back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            return back == text ? std::string() : std::string("it does not read back as written");
        });
    return r.ok ? std::string() : r.why;
}

bool check_all_sheets(const fs::path& nitrofs, const std::function<void(const std::string&)>& line) {
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(nitrofs, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (it->is_regular_file(type_ec))
            files.push_back(u8(it->path().lexically_relative(nitrofs)));
    }
    std::sort(files.begin(), files.end());
    int sheets = 0, arranged = 0, screens = 0, banks = 0, wholes = 0, problems = 0, overlapping = 0, conflicts = 0;
    for (const auto& file : files) {
        for (auto& data : game_file_data(nitrofs, file)) {
            // the sheets as names.cpp numbers them
            int count = 0;
            for (std::size_t at = 0; at + 0x18 <= data.bytes.size(); at += 4) {
                if (std::memcmp(&data.bytes[at], "RGCN", 4) == 0)
                    ++count;
            }
            for (int n = 0; n < count; ++n) {
                SheetSource s;
                if (!sheet_source(data.bytes, data.label, n, &s, nullptr))
                    break;
                borrow_arrangements(nitrofs, &s);
                ++sheets;
                const auto arrangements = fitting_arrangements(s);
                if (arrangements.size() > 1)
                    ++arranged;  // besides the sheet as it is
                const SheetPixels game = game_sheet_pixels(s);
                for (const auto& arr : arrangements) {
                    const std::string where =
                        data.label + "/chr/" + std::to_string(n) +
                        (arr.whole ? " as it is" : (arr.cells ? " cells " : " screen ") + std::to_string(arr.resource));
                    (arr.whole ? wholes : arr.cells ? banks : screens) += 1;
                    Assembly a;
                    std::string why;
                    if (!assemble(s, arr, &a, &why)) {
                        ++problems;
                        line(where + ": not assembled: " + why);
                        continue;
                    }
                    if (a.overlaps > 0)
                        ++overlapping;
                    const auto palette = arrangement_palette(s, arr);
                    // at 1x and 2x: the picture taken back whole draws the same picture
                    for (const int k : {1, 2}) {
                        const SheetPixels from = rescale(game, k);
                        const auto picture = render_assembly(s, a, from, palette);
                        TakeBack t;
                        if (!take_back(s, a, from, palette, picture, a.width * k, a.height * k, Colours::automatic,
                                       true, &t, &why)) {
                            ++problems;
                            line(where + ": not taken back: " + why);
                            break;
                        }
                        if (!t.conflicts.empty())
                            ++conflicts;
                        if (!t.sheet.indexed || t.off_palette != 0 ||
                            render_assembly(s, a, t.sheet, palette) != picture) {
                            ++problems;
                            line(where + " at " + std::to_string(k) + "x: taken back, it draws differently (" +
                                 std::to_string(t.off_palette) + " pixels off its palette)");
                            break;
                        }
                    }
                }
            }
        }
    }
    line(std::to_string(sheets) + " sheets, " + std::to_string(arranged) + " with a screen or cells that fit; " +
         std::to_string(screens) + " screens, " + std::to_string(banks) + " cell banks and " + std::to_string(wholes) +
         " sheets as they are assembled, " + std::to_string(overlapping) + " with overlapping pieces, " +
         std::to_string(conflicts) + " with one sheet pixel drawn in two colours; " + std::to_string(problems) +
         " problems");
    return problems == 0;
}

SheetChoice default_choice(const SheetSource& s, const SheetPixels& current) {
    SheetChoice c;
    const auto fits = fitting_arrangements(s);
    if (fits.empty())
        return c;
    c.palette = fits.front().palette;
    // those that show the sheet joined up first, the ones sharing out its
    // tiles chosen together
    Ranked ranked = rank_arrangements(s, fits, current);
    c.order = std::move(ranked.order);
    c.together = std::min(ranked.together, c.order.size());
    for (const auto& f : c.order) {
        if (f.whole)
            c.row = f.row, c.screen_rows = f.rows;
    }
    return c;
}

Arrangement coloured_as(Arrangement a, int palette, int row, const std::vector<int>& tile_rows) {
    a.palette = palette;
    a.row = row;
    if (a.whole)
        a.rows = tile_rows;
    return a;
}

bool default_sheet_picture(const SheetSource& s, const fs::path& mod_dir, const std::string& key,
                           std::vector<std::uint8_t>* rgba, int* w, int* h, std::string* why) {
    const SheetPixels current = current_sheet(mod_dir, key, s, nullptr);
    const SheetChoice c = default_choice(s, current);
    // the rows the modder gave its tiles, else those of the screen standing for it
    std::vector<int> rows = read_tile_rows(mod_dir, key, s.tiles);
    if (rows.empty())
        rows = c.screen_rows;
    std::vector<Arrangement> list;
    for (std::size_t i = 0; i < c.together; ++i)
        list.push_back(coloured_as(c.order[i], c.palette, c.row, rows));
    if (list.empty()) {
        if (why != nullptr)
            *why = "nothing to assemble it with";
        return false;
    }
    Assembly a;
    if (!assemble_stack(s, list, &a, nullptr, why))
        return false;
    Arrangement with_palette;
    with_palette.palette = c.palette;
    *rgba = render_assembly(s, a, current, arrangement_palette(s, with_palette));
    *w = a.width * current.scale;
    *h = a.height * current.scale;
    return true;
}

int export_sheets(const fs::path& nitrofs, const fs::path& dump, const fs::path& mod_dir, const fs::path& folder,
                  const std::function<bool(std::size_t, std::size_t, const std::string&)>& each,
                  std::vector<std::string>* failed) {
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(nitrofs, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (it->is_regular_file(type_ec))
            files.push_back(u8(it->path().lexically_relative(nitrofs)));
    }
    std::sort(files.begin(), files.end());
    int written = 0;
    for (std::size_t f = 0; f < files.size(); ++f) {
        if (!each(f, files.size(), std::string()))
            return written;
        for (auto& data : game_file_data(nitrofs, files[f])) {
            // the sheets as names.cpp numbers them
            int count = 0;
            for (std::size_t at = 0; at + 0x18 <= data.bytes.size(); at += 4) {
                if (std::memcmp(&data.bytes[at], "RGCN", 4) == 0)
                    ++count;
            }
            for (int n = 0; n < count; ++n) {
                const std::string key = data.label + "/chr/" + std::to_string(n);
                SheetSource s;
                std::string why;
                if (!sheet_source(data.bytes, data.label, n, &s, nullptr))
                    break;
                borrow_arrangements(nitrofs, &s);
                gather_nearby(nitrofs, &s);
                take_seen_colours(dump, key, &s);
                std::vector<std::uint8_t> rgba;
                int w = 0, h = 0;
                const fs::path out = folder / path_u8(key + ".png");
                if (!default_sheet_picture(s, mod_dir, key, &rgba, &w, &h, &why)) {
                    if (failed != nullptr)
                        failed->push_back(key + ": " + why);
                    continue;
                }
                fs::create_directories(out.parent_path(), ec);
                if (const char* e = khdays_png_write(u8(out).c_str(), rgba.data(), w, h)) {
                    if (failed != nullptr)
                        failed->push_back(key + ": " + e);
                    continue;
                }
                ++written;
                if (!each(f, files.size(), key))
                    return written;
            }
        }
    }
    each(files.size(), files.size(), std::string());
    return written;
}

}  // namespace studio
