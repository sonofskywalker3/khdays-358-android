// A 2D sheet assembled: a screen or the cells of its data, coloured with
// one of its palettes -- both the modder's choice --, exported as a picture,
// the edited picture brought back and shown as it will be, with the tiles
// edited twice, repeated or flipped marked, and saved as the sheet the port
// reads.
#include "../app.h"
#include "../save_dialog.h"

#include "core/files/paths.h"
#include "core/sheets/sheets.h"

#include "khdays/assets/cell.h"
#include "khdays/vfs/filesystem.h"

#include "imgui.h"

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <set>

namespace studio {

namespace {

namespace assets = khdays::assets;

// the marks, as the view outlines them and the exported grid draws them
constexpr ImU32 kUnseen = IM_COL32(90, 170, 255, 255);
constexpr ImU32 kRepeated = IM_COL32(240, 200, 60, 255);
constexpr ImU32 kConflict = IM_COL32(240, 70, 70, 255);
constexpr ImU32 kGrid = IM_COL32(128, 128, 128, 255);

struct SheetView {
    bool assembled = true;  // the mode, kept from one sheet to the next
    int item = -1;
    std::string mod;
    std::string error;
    SheetSource source;
    std::vector<Arrangement> fits;
    std::vector<bool> chosen;  // per fit: in the picture
    struct Thumb {
        std::vector<std::uint8_t> rgba;
        int w = 0, h = 0;
    };
    std::vector<Thumb> thumbs;  // per fit, at its own size
    int palette = -1;
    int row = 0;  // the palette row of the sheet as it is (its tiles with none of their own)
    // the sheet as it is: each tile's own palette row (-1: `row`), from the
    // screen that stands for it or given by the modder, kept in the mod
    std::vector<int> tile_rows, screen_rows;
    bool painting = false, dragging = false;
    ImVec2 drag_from{};                // in the assembly's pixels
    int whole_top = 0, whole_end = 0;  // where the sheet as it is lies in the picture
    Assembly assembly;
    assets::Palette2D colours;
    SheetPixels current;
    fs::path current_from;
    std::vector<std::uint8_t> picture;
    // the game's own sheet, shown as the original whatever the mod holds
    SheetPixels game;
    std::vector<std::uint8_t> game_picture;
    std::uint64_t version = 0;
    int export_scale = 0;  // 0: the current sheet's

    fs::path edited_file;
    std::vector<std::uint8_t> edited;
    int ew = 0, eh = 0;
    Colours mode = Colours::automatic;
    bool have_result = false;
    TakeBack result;
    std::string result_error;
    std::vector<std::uint8_t> result_picture;
    std::uint64_t result_version = 0;
    bool marks = true;
    // the tiles that draw something and were never seen in play (in the greys
    // of the palette of the colours seen), outlined
    std::vector<int> unseen;
    bool mark_unseen = true;
};

SheetView v;

// rows given with no mod chosen, by sheet, until one is
std::map<std::string, std::vector<int>> rows_without_mod;

fs::path nitrofs() {
    return khdays::vfs::data_root() / "nitrofs";
}

fs::path mod_dir(App& app) {
    const Mod* m = app.current_mod();
    return m != nullptr ? m->dir : fs::path();
}

void take_edited() {
    v.have_result = false;
    v.result_error.clear();
    ++v.result_version;
    if (v.edited.empty())
        return;
    v.have_result = take_back(v.source, v.assembly, v.current, v.colours, v.edited, v.ew, v.eh, v.mode, false,
                              &v.result, &v.result_error);
    if (v.have_result)
        v.result_picture = render_assembly(v.source, v.assembly, v.result.sheet, v.colours);
}

// The picture again: the arrangements chosen, one under the other, the sheet
// as the mod has it; and each arrangement's own small picture.
void rebuild(App& app) {
    ++v.version;
    v.picture.clear();
    v.game_picture.clear();
    v.assembly = Assembly{};
    v.error.clear();
    if (v.fits.empty())
        return;
    Arrangement with_palette;
    with_palette.palette = v.palette;
    v.colours = arrangement_palette(v.source, with_palette);
    v.current =
        current_sheet(mod_dir(app), app.catalog.items[static_cast<std::size_t>(v.item)].key, v.source, &v.current_from);
    const SheetPixels small = rescale(v.current, 1);
    v.thumbs.assign(v.fits.size(), {});
    std::vector<Arrangement> list;
    for (std::size_t i = 0; i < v.fits.size(); ++i) {
        const Arrangement a = coloured_as(v.fits[i], v.palette, v.row, v.tile_rows);
        Assembly one;
        if (assemble(v.source, a, &one, nullptr)) {
            v.thumbs[i] = {render_assembly(v.source, one, small, v.colours), one.width, one.height};
        }
        if (v.chosen[i])
            list.push_back(a);
    }
    std::string why;
    if (list.empty())
        return;
    std::vector<int> tops;
    if (!assemble_stack(v.source, list, &v.assembly, &tops, &why)) {
        v.error = why;
        return;
    }
    v.whole_top = v.whole_end = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].whole)
            v.whole_top = tops[i], v.whole_end = tops[i] + v.source.height();
    }
    v.picture = render_assembly(v.source, v.assembly, v.current, v.colours);
    v.game = v.current_from.empty() ? v.current : game_sheet_pixels(v.source);
    v.game_picture = v.current_from.empty() ? v.picture : render_assembly(v.source, v.assembly, v.game, v.colours);
    v.unseen.clear();
    if (v.palette == v.source.seen_palette) {
        for (int t = 0; t < v.source.tiles; ++t) {
            const int own =
                static_cast<std::size_t>(t) < v.tile_rows.size() ? v.tile_rows[static_cast<std::size_t>(t)] : -1;
            if ((own < 0 ? v.row : own) == kUnseenRow && tile_draws(v.source, t))
                v.unseen.push_back(t);
        }
    }
    take_edited();
}

void open(App& app) {
    const bool assembled = v.assembled;
    v = SheetView{};
    v.assembled = assembled;
    v.item = app.selected;
    v.mod = app.current_mod() != nullptr ? app.current_mod()->name : std::string();
    const Item& item = app.catalog.items[static_cast<std::size_t>(v.item)];
    std::string why;
    if (!load_sheet_source(nitrofs(), item.key, &v.source, &why)) {
        v.error = why;
        return;
    }
    // the colours the game was seen drawing it with, from the graphics dump
    take_seen_colours(resolve(app.settings, app.settings.graphics_dump), item.key, &v.source);
    const SheetChoice choice = default_choice(v.source, current_sheet(mod_dir(app), item.key, v.source, nullptr));
    v.fits = choice.order;
    v.palette = choice.palette;
    v.row = choice.row;
    v.screen_rows = choice.screen_rows;
    // the sheet as it is: the rows the modder gave its tiles (in the mod, or
    // given before a mod was chosen), else those of the screen standing for it
    v.tile_rows = read_tile_rows(mod_dir(app), item.key, v.source.tiles);
    if (const auto kept = rows_without_mod.find(item.key); v.tile_rows.empty() && kept != rows_without_mod.end()) {
        v.tile_rows = kept->second;
    }
    if (v.tile_rows.empty())
        v.tile_rows = v.screen_rows;
    v.chosen.assign(v.fits.size(), false);
    for (std::size_t i = 0; i < choice.together; ++i)
        v.chosen[i] = true;
    rebuild(app);
}

std::string arrangement_name(App& app, const Arrangement& a) {
    try {
        if (a.whole)
            return fmt(app.language, S::sheet_whole, {std::to_string(v.row)});
        if (!a.cells) {
            const auto& r = v.source.screens[static_cast<std::size_t>(a.resource)];
            const auto map = assets::decode_nscr(&v.source.data[r.at], r.size);
            return fmt(app.language, S::sheet_screen,
                       {std::to_string(a.resource), std::to_string(map.width_tiles * 8),
                        std::to_string(map.height_tiles * 8)});
        }
        const auto& r = v.source.cell_banks[static_cast<std::size_t>(a.resource)];
        const auto bank = assets::decode_ncer(&v.source.data[r.at], r.size);
        return fmt(app.language, S::sheet_cells, {std::to_string(a.resource), std::to_string(bank.cells.size())});
    } catch (const std::exception&) {
        return "?";
    }
}

struct Shown {
    ImVec2 at{};       // the picture's top left on the screen
    float factor = 0;  // screen pixels a picture pixel
};

// Space down to `y` (window coordinates), as an item, so the layout grows with it.
void fill_to(float y) {
    const float rest = y - ImGui::GetCursorPosY();
    if (rest > 0)
        ImGui::Dummy(ImVec2(1.0f, rest));
}

// A picture fitted into `box`, sharp, over a checkerboard.
Shown show(App& app, const std::string& name, std::uint64_t version, const std::vector<std::uint8_t>& rgba, int w,
           int h, ImVec2 box) {
    Shown s;
    const ImVec2 start = ImGui::GetCursorPos();
    const GpuImage& image = app.images->pixels(name, version, rgba, w, h);
    if (image.texture == nullptr) {
        ImGui::TextWrapped("%s", image.error.c_str());
        fill_to(start.y + box.y);
        return s;
    }
    const float k = std::min(box.x / static_cast<float>(w), box.y / static_cast<float>(h));
    const float f = k >= 1.0f ? std::floor(k) : k;
    const ImVec2 size(static_cast<float>(w) * f, static_cast<float>(h) * f);
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    s.at = ImGui::GetCursorScreenPos();
    s.factor = f;
    const float cell = 8.0f;
    for (float y = 0; y < size.y; y += cell) {
        for (float x = 0; x < size.x; x += cell) {
            const bool dark = (static_cast<int>(x / cell) + static_cast<int>(y / cell)) % 2 != 0;
            list->AddRectFilled(ImVec2(s.at.x + x, s.at.y + y),
                                ImVec2(s.at.x + std::min(x + cell, size.x), s.at.y + std::min(y + cell, size.y)),
                                dark ? IM_COL32(60, 60, 64, 255) : IM_COL32(90, 90, 96, 255));
        }
    }
    if (pio.DrawCallback_SetSamplerNearest != nullptr)
        list->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(image.texture))), size);
    if (pio.DrawCallback_SetSamplerLinear != nullptr)
        list->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
    fill_to(start.y + box.y);
    return s;
}

// The tiles in `tiles` outlined where the assembly places them.
void mark(const Shown& s, int scale, const std::vector<int>& tiles, ImU32 colour) {
    if (s.factor <= 0 || tiles.empty())
        return;
    const std::set<int> wanted(tiles.begin(), tiles.end());
    ImDrawList* list = ImGui::GetWindowDrawList();
    const float k = s.factor * static_cast<float>(scale);
    for (const auto& spot : v.assembly.spots) {
        if (wanted.count(spot.tile) == 0)
            continue;
        const ImVec2 a(s.at.x + static_cast<float>(spot.x) * k, s.at.y + static_cast<float>(spot.y) * k);
        if (8 * k >= 8) {
            list->AddRect(a, ImVec2(a.x + 8 * k, a.y + 8 * k), colour, 0.0f, 0, 2.0f);
        } else {  // too small to outline: a light wash, the picture still seen
            list->AddRectFilled(a, ImVec2(a.x + 8 * k, a.y + 8 * k), (colour & 0x00FFFFFFu) | 0x50000000u);
        }
    }
}

// The rows given kept: in the mod, or until one is chosen.
void keep_rows(App& app) {
    const std::string& key = app.catalog.items[static_cast<std::size_t>(v.item)].key;
    const fs::path dir = mod_dir(app);
    if (dir.empty()) {
        rows_without_mod[key] = v.tile_rows;
        return;
    }
    const std::string e = write_tile_rows(dir, key, v.tile_rows == v.screen_rows ? std::vector<int>() : v.tile_rows);
    if (!e.empty())
        app.tell(fmt(app.language, S::save_failed, {e}), false);
}

// Painting rows on the sheet as it is in the picture `s` (the game's own
// sheet, at its scale): the tile under the pointer outlined; a rectangle
// dragged gives its tiles the row chosen.
void paint_rows(App& app, const Shown& s) {
    if (s.factor <= 0 || v.whole_end <= v.whole_top)
        return;
    const float k = s.factor * static_cast<float>(v.game.scale);
    const ImVec2 m = ImGui::GetIO().MousePos;
    // the pointer in the assembly's pixels, kept inside the sheet as it is
    const float px = (m.x - s.at.x) / k, py = (m.y - s.at.y) / k;
    const bool inside = ImGui::IsWindowHovered() && px >= 0 && py >= static_cast<float>(v.whole_top) &&
                        px < static_cast<float>(v.assembly.width) && py < static_cast<float>(v.whole_end);
    ImDrawList* list = ImGui::GetWindowDrawList();
    auto box = [&](float x0, float y0, float x1, float y1, ImU32 colour) {
        list->AddRect(ImVec2(s.at.x + x0 * k, s.at.y + y0 * k), ImVec2(s.at.x + x1 * k, s.at.y + y1 * k), colour, 0.0f,
                      0, 2.0f);
    };
    if (inside && !v.dragging) {
        const float tx = std::floor(px / 8) * 8,
                    ty = std::floor((py - static_cast<float>(v.whole_top)) / 8) * 8 + static_cast<float>(v.whole_top);
        box(tx, ty, tx + 8, ty + 8, IM_COL32(80, 160, 255, 255));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            v.dragging = true, v.drag_from = ImVec2(px, py);
    }
    if (!v.dragging)
        return;
    const float cx = std::clamp(px, 0.0f, static_cast<float>(v.assembly.width));
    const float cy = std::clamp(py, static_cast<float>(v.whole_top), static_cast<float>(v.whole_end));
    const float x0 = std::min(v.drag_from.x, cx), x1 = std::max(v.drag_from.x, cx);
    const float y0 = std::min(v.drag_from.y, cy), y1 = std::max(v.drag_from.y, cy);
    box(x0, y0, x1, y1, IM_COL32(80, 160, 255, 255));
    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        return;
    v.dragging = false;
    if (v.tile_rows.size() != static_cast<std::size_t>(v.source.tiles))
        v.tile_rows.resize(static_cast<std::size_t>(v.source.tiles), -1);
    bool any = false;
    for (const auto& spot : v.assembly.spots) {
        const float sx = static_cast<float>(spot.x), sy = static_cast<float>(spot.y);
        if (sy < static_cast<float>(v.whole_top) || sy >= static_cast<float>(v.whole_end))
            continue;
        if (sx + 8 <= x0 || sx >= std::max(x1, x0 + 0.01f) || sy + 8 <= y0 || sy >= std::max(y1, y0 + 0.01f))
            continue;
        v.tile_rows[static_cast<std::size_t>(spot.tile)] = v.row;
        any = true;
    }
    if (!any)
        return;
    keep_rows(app);
    rebuild(app);
}

// The tiles' numbers, with how many places use each when `uses`.
std::string tile_list(const std::vector<int>& tiles, bool uses = false) {
    std::string out;
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (i == 24) {
            out += " ... (" + std::to_string(tiles.size()) + ")";
            break;
        }
        out += (i == 0 ? "" : ", ") + std::to_string(tiles[i]);
        if (uses)
            out += " x" + std::to_string(v.assembly.uses[static_cast<std::size_t>(tiles[i])]);
    }
    return out;
}

void save(App& app) {
    const Mod* m = app.current_mod();
    if (m == nullptr || !v.have_result)
        return;
    const Item& item = app.catalog.items[static_cast<std::size_t>(v.item)];
    fs::path written;
    std::string why;
    if (!save_sheet(m->dir, item.key, v.source, v.result.sheet, display_colours(v.source, v.assembly, v.colours),
                    &written, &why)) {
        app.tell(fmt(app.language, S::save_failed, {why}), false);
        return;
    }
    app.images->forget(written);
    app.tell(fmt(app.language, S::sheet_saved, {u8(written.lexically_relative(app.settings.root))}) + "  " +
                 tr(app.language, S::restart_note),
             true);
    v.edited.clear();
    v.edited_file.clear();
    rebuild(app);  // the mod's sheet is the current one now
}

}  // namespace

bool sheet_offer(App& app, const fs::path& file) {
    if (!v.assembled || v.item != app.selected || v.assembly.width == 0)
        return false;
    const std::string path = u8(file);
    const char* error = nullptr;
    int w = 0, h = 0;
    std::uint8_t* px = khdays_png_read(path.c_str(), &w, &h, &error);
    v.edited_file = file;
    v.edited.clear();
    if (px == nullptr) {
        v.have_result = false;
        v.result_error = error != nullptr ? error : "unreadable";
        return true;
    }
    v.edited.assign(px, px + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    khdays_png_free(px);
    v.ew = w;
    v.eh = h;
    v.mode = Colours::automatic;
    take_edited();
    return true;
}

void sheet_export_to(App& app, const fs::path& chosen) {
    if (v.item != app.selected || v.assembly.width == 0)
        return;
    fs::path file = chosen;
    if (lower(u8(file.extension())) != ".png")
        file += ".png";
    const SheetPixels p = v.export_scale > 0 ? rescale(v.current, v.export_scale) : v.current;
    const auto rgba = render_assembly(v.source, v.assembly, p, v.colours);
    const char* e =
        khdays_png_write(u8(file).c_str(), rgba.data(), v.assembly.width * p.scale, v.assembly.height * p.scale);
    app.tell(e != nullptr ? fmt(app.language, S::save_failed, {e}) : fmt(app.language, S::sheet_exported, {u8(file)}),
             e == nullptr);
}

void sheet_grid_export_to(App& app, const fs::path& chosen) {
    if (v.item != app.selected || v.assembly.width == 0)
        return;
    fs::path file = chosen;
    if (lower(u8(file.extension())) != ".png")
        file += ".png";
    const int scale = v.export_scale > 0 ? v.export_scale : v.current.scale;
    std::vector<TileMark> marks;  // those the view shows
    if (v.mark_unseen)
        marks.push_back({v.unseen, kUnseen});
    if (v.have_result && v.marks) {
        marks.push_back({v.result.repeated, kRepeated});
        marks.push_back({v.result.conflicts, kConflict});
    }
    const auto rgba = render_tile_grid(v.assembly, scale, kGrid, marks);
    const char* e =
        khdays_png_write(u8(file).c_str(), rgba.data(), v.assembly.width * scale, v.assembly.height * scale);
    app.tell(e != nullptr ? fmt(app.language, S::save_failed, {e}) : fmt(app.language, S::sheet_exported, {u8(file)}),
             e == nullptr);
}

bool draw_sheet_mode(App& app) {
    if (ImGui::RadioButton(tr(app.language, S::sheet_as_tiles), !v.assembled))
        v.assembled = false;
    same_line_if_room(check_width(tr(app.language, S::sheet_assembled)));
    if (ImGui::RadioButton(tr(app.language, S::sheet_assembled), v.assembled))
        v.assembled = true;
    return v.assembled;
}

void draw_sheet_assembled(App& app, const Item& item) {
    const std::string mod = app.current_mod() != nullptr ? app.current_mod()->name : std::string();
    if (v.item != app.selected || v.mod != mod)
        open(app);
    if (!v.error.empty() && v.fits.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", v.error.c_str());
        return;
    }
    if (v.fits.empty()) {
        ImGui::TextWrapped("%s", tr(app.language, S::sheet_no_arrangement));
        return;
    }

    // the choice: the arrangements in the picture (each shown small, ticked to
    // be in it, one under the other), and the palette
    ImGui::TextUnformatted(tr(app.language, S::sheet_arrangement));
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    const float thumb_w = 112.0f * dpi, thumb_h = 84.0f * dpi;
    // two rows of them at most in sight, the rest scrolled to
    const float row_h = thumb_h + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0),
                                        ImVec2(FLT_MAX, row_h * 2.0f + ImGui::GetStyle().WindowPadding.y * 2));
    ImGui::BeginChild("##thumbs", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
    const float right_edge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    bool changed = false;
    for (std::size_t i = 0; i < v.fits.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string name = arrangement_name(app, v.fits[i]);
        const float item_w = std::max(thumb_w, ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                                                   ImGui::CalcTextSize(name.c_str()).x);
        if (i > 0) {
            ImGui::SameLine();
            if (ImGui::GetCursorScreenPos().x + item_w > right_edge)
                ImGui::NewLine();
        }
        ImGui::BeginGroup();
        const auto& t = v.thumbs[i];
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##thumb", ImVec2(thumb_w, thumb_h))) {
            v.chosen[i] = !v.chosen[i];
            changed = true;
        }
        ImDrawList* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(at, ImVec2(at.x + thumb_w, at.y + thumb_h), IM_COL32(40, 40, 44, 255));
        if (t.w > 0) {
            const GpuImage& g = app.images->pixels("sheet:" + std::to_string(v.item) + ":thumb:" + std::to_string(i),
                                                   v.version, t.rgba, t.w, t.h);
            if (g.texture != nullptr) {
                const float k =
                    std::min((thumb_w - 4) / static_cast<float>(t.w), (thumb_h - 4) / static_cast<float>(t.h));
                const ImVec2 size(static_cast<float>(t.w) * k, static_cast<float>(t.h) * k);
                const ImVec2 p(at.x + (thumb_w - size.x) * 0.5f, at.y + (thumb_h - size.y) * 0.5f);
                list->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(g.texture))), p,
                               ImVec2(p.x + size.x, p.y + size.y));
            }
        }
        list->AddRect(at, ImVec2(at.x + thumb_w, at.y + thumb_h),
                      v.chosen[i] ? IM_COL32(80, 160, 255, 255) : IM_COL32(90, 90, 96, 255), 0.0f, 0,
                      v.chosen[i] ? 3.0f : 1.0f);
        bool on = v.chosen[i];
        if (ImGui::Checkbox(name.c_str(), &on)) {
            v.chosen[i] = on;
            changed = true;
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (changed)
        rebuild(app);
    auto palette_name = [&](int i) {
        if (i < 0)
            return std::string(tr(app.language, S::sheet_no_palette));
        if (i == v.source.seen_palette)
            return fmt(app.language, S::sheet_palette_seen, {std::to_string(i)});
        const std::string from = palette_from(v.source, i);
        return from.empty() ? fmt(app.language, S::sheet_palette_n, {std::to_string(i)})
                            : fmt(app.language, S::sheet_palette_from, {std::to_string(i), from});
    };
    const std::string palette_now = palette_name(v.palette);
    ImGui::SetNextItemWidth(std::max(200.0f * ImGui::GetStyle().FontScaleDpi,
                                     ImGui::CalcTextSize(palette_now.c_str()).x + ImGui::GetFrameHeight() * 2));
    if (ImGui::BeginCombo(tr(app.language, S::sheet_palette), palette_now.c_str())) {
        for (int i = -1; i < palette_count(v.source); ++i) {
            const std::string name = palette_name(i) + "##p" + std::to_string(i);
            if (ImGui::Selectable(name.c_str(), i == v.palette)) {
                v.palette = i;
                Arrangement with_palette;
                with_palette.palette = i;
                v.row = i < 0 ? 0 : liveliest_row(v.source, arrangement_palette(v.source, with_palette));
                rebuild(app);
            }
        }
        ImGui::EndCombo();
    }
    // the sheet as it is: which palette row draws it (the game gives each tile
    // its own: the modder paints them on the picture, once, kept in the mod)
    bool whole_chosen = false;
    for (std::size_t i = 0; i < v.fits.size(); ++i)
        whole_chosen = whole_chosen || (v.fits[i].whole && v.chosen[i]);
    const int rows = palette_rows(v.source, v.colours);
    if (whole_chosen && rows > 1) {
        same_line_if_room(140.0f * ImGui::GetStyle().FontScaleDpi + check_width(tr(app.language, S::sheet_row)));
        ImGui::SetNextItemWidth(140.0f * ImGui::GetStyle().FontScaleDpi);
        if (ImGui::SliderInt(tr(app.language, S::sheet_row), &v.row, 0, rows - 1)) {
            v.row = std::clamp(v.row, 0, rows - 1);
            rebuild(app);
        }
        same_line_if_room(check_width(tr(app.language, S::sheet_paint_rows)));
        ImGui::Checkbox(tr(app.language, S::sheet_paint_rows), &v.painting);
        const bool painted = std::any_of(v.tile_rows.begin(), v.tile_rows.end(), [](int r) { return r >= 0; }) &&
                             v.tile_rows != v.screen_rows;
        if (painted) {
            same_line_if_room(button_width(tr(app.language, S::sheet_clear_rows)));
            if (ImGui::Button(tr(app.language, S::sheet_clear_rows))) {
                v.tile_rows = v.screen_rows;
                keep_rows(app);
                rebuild(app);
            }
        }
        if (v.painting) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("%s", tr(app.language, S::sheet_paint_note));
            ImGui::PopStyleColor();
        }
    } else {
        v.painting = v.dragging = false;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    // screens and cells taken from a twin or the folder, when one of them fits
    const bool any_borrowed = std::any_of(v.fits.begin(), v.fits.end(), [](const Arrangement& a) { return !a.whole; });
    if (!v.source.borrowed.empty() && any_borrowed) {
        ImGui::TextWrapped("%s", fmt(app.language, S::sheet_borrowed, {v.source.borrowed}).c_str());
    }
    ImGui::TextWrapped("%s", tr(app.language, S::sheet_choice_note));
    ImGui::PopStyleColor();
    ImGui::TextUnformatted(v.current_from.empty()
                               ? tr(app.language, S::sheet_from_game)
                               : fmt(app.language, S::sheet_from_mod, {std::to_string(v.current.scale)}).c_str());
    if (v.assembly.overlaps > 0) {
        ImGui::TextDisabled("%s", fmt(app.language, S::sheet_overlaps, {std::to_string(v.assembly.overlaps)}).c_str());
    }
    // the tiles the game was never seen drawing: in greys, not in guessed colours
    if (v.palette == v.source.seen_palette && v.source.unseen_tiles > 0) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "%s",
                           fmt(app.language, S::sheet_unseen_tiles,
                               {std::to_string(v.source.unseen_tiles), std::to_string(kUnseenRow)})
                               .c_str());
        ImGui::PopTextWrapPos();
        ImGui::Checkbox(tr(app.language, S::sheet_mark_unseen), &v.mark_unseen);
    }

    if (v.assembly.width == 0) {  // none ticked
        if (!v.error.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", v.error.c_str());
        return;
    }

    // the pictures: now, and as it will be
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    // as tall as the room left, but for the lines under the pictures
    const float lines = (v.edited_file.empty() ? 3.0f : 9.0f) * ImGui::GetFrameHeightWithSpacing();
    const float box_h = std::max(180.0f, ImGui::GetContentRegionAvail().y - lines);
    const std::string key = "sheet:" + std::to_string(v.item);
    if (ImGui::BeginTable("##assembled", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(tr(app.language, S::original));
        const Shown game = show(app, key + ":game", v.version, v.game_picture, v.assembly.width * v.game.scale,
                                v.assembly.height * v.game.scale, ImVec2(half, box_h));
        if (v.painting)
            paint_rows(app, game);
        if (v.mark_unseen)
            mark(game, v.game.scale, v.unseen, kUnseen);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(tr(app.language, S::replacement));
        if (!v.have_result && !v.current_from.empty()) {
            // what the mod holds now, until another picture is brought back
            show(app, key + ":now", v.version, v.picture, v.assembly.width * v.current.scale,
                 v.assembly.height * v.current.scale, ImVec2(half, box_h));
        } else if (v.have_result) {
            const Shown s =
                show(app, key + ":new", v.result_version, v.result_picture, v.assembly.width * v.result.scale,
                     v.assembly.height * v.result.scale, ImVec2(half, box_h));
            if (v.marks) {
                mark(s, v.result.scale, v.result.repeated, kRepeated);
                mark(s, v.result.scale, v.result.conflicts, kConflict);
            }
        } else {
            ImGui::Dummy(ImVec2(half, box_h));
        }
        ImGui::EndTable();
    }

    // export
    ImGui::SetNextItemWidth(110.0f * ImGui::GetStyle().FontScaleDpi);
    const std::string size_now =
        v.export_scale == 0 ? std::to_string(v.current.scale) + "x" : std::to_string(v.export_scale) + "x";
    if (ImGui::BeginCombo(tr(app.language, S::sheet_export_size), size_now.c_str())) {
        for (int k = 1; k <= 8; ++k) {
            if (ImGui::Selectable((std::to_string(k) + "x").c_str(),
                                  (v.export_scale == 0 ? v.current.scale : v.export_scale) == k)) {
                v.export_scale = k;
            }
        }
        ImGui::EndCombo();
    }
    same_line_if_room(button_width(tr(app.language, S::sheet_export)));
    if (ImGui::Button(tr(app.language, S::sheet_export)))
        choose_export(app);
    same_line_if_room(button_width(tr(app.language, S::sheet_export_grid)));
    if (ImGui::Button(tr(app.language, S::sheet_export_grid))) {
        std::string name = export_name(item);
        const std::size_t dot = name.rfind('.');
        name.insert(dot == std::string::npos ? name.size() : dot, "_grid");  // before its ".png"
        choose_save_file(app, name, "PNG", "png", app.dialog_grids);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr(app.language, S::sheet_export_grid_hint));
    same_line_if_room(button_width(tr(app.language, S::sheet_bring_back)));
    if (ImGui::Button(tr(app.language, S::sheet_bring_back)))
        choose_file(app, app.catalog.items[static_cast<std::size_t>(v.item)]);
    same_line_if_room(button_width(tr(app.language, S::sheets_export_all)));
    ImGui::BeginDisabled(app.bulk_export != nullptr && !app.bulk_export->done);
    if (ImGui::Button(tr(app.language, S::sheets_export_all))) {
        SDL_ShowOpenFolderDialog(
            [](void* user, const char* const* folders, int) {
                if (folders == nullptr || folders[0] == nullptr)
                    return;
                App* a = static_cast<App*>(user);
                std::lock_guard lock(a->dialog_mutex);
                a->dialog_sheet_folders.emplace_back(folders[0]);
            },
            &app, app.window, nullptr, false);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tr(app.language, S::sheets_export_all_note));

    // the edited picture
    if (v.edited_file.empty())
        return;
    ImGui::Separator();
    if (!v.have_result) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s: %s", u8(v.edited_file.filename()).c_str(),
                           v.result_error.c_str());
        return;
    }
    const TakeBack& t = v.result;
    ImGui::TextUnformatted(
        fmt(app.language, S::sheet_edited, {u8(v.edited_file.filename()), std::to_string(t.scale)}).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", fmt(app.language, S::sheet_changed, {std::to_string(t.changed)}).c_str());
    const ImVec4 warn(0.95f, 0.80f, 0.35f, 1);
    if (t.outside > 0)
        ImGui::TextColored(warn, "%s", fmt(app.language, S::sheet_outside, {std::to_string(t.outside)}).c_str());
    if (t.off_palette > 0) {
        ImGui::TextColored(warn, "%s",
                           fmt(app.language, S::sheet_off_palette, {std::to_string(t.off_palette)}).c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(app.language, S::sheet_colours));
        same_line_if_room(check_width(tr(app.language, S::sheet_colours_own)));
        if (ImGui::RadioButton(tr(app.language, S::sheet_colours_own), v.mode != Colours::fit)) {
            v.mode = Colours::automatic;
            take_edited();
        }
        same_line_if_room(check_width(tr(app.language, S::sheet_colours_fit)));
        if (ImGui::RadioButton(tr(app.language, S::sheet_colours_fit), v.mode == Colours::fit)) {
            v.mode = Colours::fit;
            take_edited();
        }
    }
    ImGui::TextWrapped("%s", tr(app.language, v.result.sheet.indexed ? S::sheet_kept_indexed : S::sheet_own_colours));
    auto flag = [&](S what, const std::vector<int>& tiles) {
        if (!tiles.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, warn);
            ImGui::TextWrapped("%s", fmt(app.language, what, {tile_list(tiles, what == S::sheet_repeated)}).c_str());
            ImGui::PopStyleColor();
        }
    };
    flag(S::sheet_conflicts, t.conflicts);
    flag(S::sheet_repeated, t.repeated);
    flag(S::sheet_flipped, t.flipped);
    flag(S::sheet_several_rows, t.several_rows);
    if (t.unplaced_coloured > 0) {
        ImGui::TextColored(warn, "%s",
                           fmt(app.language, S::sheet_unplaced, {std::to_string(t.unplaced_coloured)}).c_str());
    }
    if (!t.conflicts.empty() || !t.repeated.empty())
        ImGui::Checkbox(tr(app.language, S::sheet_marks), &v.marks);
    if (app.current_mod() == nullptr) {
        ImGui::TextColored(warn, "%s", tr(app.language, S::choose_mod_first));
        return;
    }
    ImGui::BeginDisabled(t.changed == 0);
    if (ImGui::Button(tr(app.language, S::sheet_save)))
        save(app);
    ImGui::EndDisabled();
}

std::function<bool(std::size_t, std::size_t, const std::string&)> bulk_progress(App::BulkExport* job) {
    return [job](std::size_t done, std::size_t files, const std::string& key) {
        job->files_done = done;
        job->files = files;
        if (!key.empty()) {
            ++job->written;
            std::lock_guard lock(job->mutex);
            job->current = key;
        }
        return !job->stop.load();
    };
}

void bulk_finish(App::BulkExport* job, int written, std::vector<std::string> failed) {
    {
        std::lock_guard lock(job->mutex);
        job->failed = std::move(failed);
    }
    job->written = written;
    job->done = true;
}

void sheets_export_to(App& app, const fs::path& folder) {
    if (app.bulk_export != nullptr && !app.bulk_export->done)
        return;
    app.bulk_export = std::make_unique<App::BulkExport>();
    App::BulkExport* job = app.bulk_export.get();
    job->folder = folder;
    const fs::path dump = resolve(app.settings, app.settings.graphics_dump), mod = mod_dir(app), game = nitrofs();
    job->thread = std::thread([job, dump, mod, game]() {
        std::vector<std::string> failed;
        const int written = export_sheets(game, dump, mod, job->folder, bulk_progress(job), &failed);
        bulk_finish(job, written, std::move(failed));
    });
}

void draw_bulk_export(App& app) {
    if (app.bulk_export == nullptr)
        return;
    App::BulkExport& job = *app.bulk_export;
    std::string current;
    std::vector<std::string> failed;
    {
        std::lock_guard lock(job.mutex);
        current = job.current;
        failed = job.failed;
    }
    const std::string written = std::to_string(job.written.load()), folder = u8(job.folder);
    const S exported = job.textures ? S::textures_exported : S::sheets_exported;
    if (!job.done) {
        ImGui::TextUnformatted(
            fmt(app.language, job.textures ? S::textures_exporting : S::sheets_exporting, {written}).c_str());
        const std::size_t files = job.files.load();
        ImGui::ProgressBar(files > 0 ? static_cast<float>(job.files_done.load()) / static_cast<float>(files) : 0.0f,
                           ImVec2(-1, 0));
        if (!current.empty())
            ImGui::TextDisabled("%s", current.c_str());
        if (ImGui::Button(tr(app.language, S::stop)))
            job.stop = true;
    } else {
        if (!job.told) {
            job.told = true;
            app.tell(fmt(app.language, exported, {written, folder}), failed.empty());
        }
        ImGui::TextWrapped("%s", fmt(app.language, exported, {written, folder}).c_str());
        if (!failed.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "%s",
                               fmt(app.language, S::sheets_not_written, {std::to_string(failed.size())}).c_str());
            if (ImGui::IsItemHovered()) {
                std::string list;
                for (std::size_t i = 0; i < failed.size() && i < 20; ++i)
                    list += failed[i] + "\n";
                ImGui::SetTooltip("%s", list.c_str());
            }
        }
        if (ImGui::Button(tr(app.language, S::open_folder)))
            SDL_OpenURL(("file:///" + folder).c_str());
        same_line_if_room(button_width(tr(app.language, S::close)));
        if (ImGui::Button(tr(app.language, S::close))) {
            app.bulk_export.reset();
            return;
        }
    }
    ImGui::Separator();
}

void choose_export(App& app) {
    if (v.item < 0 || v.item >= static_cast<int>(app.catalog.items.size()))
        return;
    choose_save_file(app, export_name(app.catalog.items[static_cast<std::size_t>(v.item)]), "PNG", "png",
                     app.dialog_exports);
}

}  // namespace studio
