// The shown item (right side): what it is, which mod's file the game uses,
// and for pictures the original beside the replacement, checked and saved as
// the port reads it.
#include "../app.h"
#include "../save_dialog.h"

#include "core/files/file_write.h"
#include "core/files/paths.h"
#include "core/models/texture_export.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace studio {

namespace {

// A picture fitted into `box`, its pixels kept sharp.
// Space down to `y` (window coordinates), as an item, so the layout grows
// with it.
void fill_to(float y) {
    const float rest = y - ImGui::GetCursorPosY();
    if (rest > 0)
        ImGui::Dummy(ImVec2(1.0f, rest));
}

// The box is always taken whole, so what follows stays in place whatever the
// picture's shape.
void draw_picture(App& app, const fs::path& png, ImVec2 box) {
    const ImVec2 start = ImGui::GetCursorPos();
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const GpuImage image = app.images->get(png);
    if (image.texture == nullptr) {
        ImGui::TextWrapped("%s", image.error.c_str());
        fill_to(start.y + box.y + line);
        return;
    }
    const float k = std::min(box.x / static_cast<float>(image.width), box.y / static_cast<float>(image.height));
    const float scale = k >= 1.0f ? std::floor(k) : k;  // whole multiples when enlarging
    const ImVec2 size(static_cast<float>(image.width) * scale, static_cast<float>(image.height) * scale);
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    // a checkerboard under it, so transparency shows
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float cell = 8.0f;
    for (float y = 0; y < size.y; y += cell) {
        for (float x = 0; x < size.x; x += cell) {
            const bool dark = (static_cast<int>(x / cell) + static_cast<int>(y / cell)) % 2 != 0;
            list->AddRectFilled(ImVec2(at.x + x, at.y + y),
                                ImVec2(at.x + std::min(x + cell, size.x), at.y + std::min(y + cell, size.y)),
                                dark ? IM_COL32(60, 60, 64, 255) : IM_COL32(90, 90, 96, 255));
        }
    }
    if (pio.DrawCallback_SetSamplerNearest != nullptr)
        list->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(image.texture))), size);
    if (pio.DrawCallback_SetSamplerLinear != nullptr)
        list->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
    ImGui::TextDisabled(
        "%s", fmt(app.language, S::size, {std::to_string(image.width), std::to_string(image.height)}).c_str());
    fill_to(start.y + box.y + line);
}

ImVec4 verdict_colour(Verdict v) {
    switch (v) {
    case Verdict::accepted: return ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
    case Verdict::warned: return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
    case Verdict::refused: return ImVec4(0.95f, 0.40f, 0.40f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

S verdict_word(Verdict v) {
    switch (v) {
    case Verdict::accepted: return S::accepted;
    case Verdict::warned: return S::warned;
    case Verdict::refused: return S::refused;
    }
    return S::accepted;
}

// Saves the candidate into the mod, as the port reads it.
void save(App& app, const Item& item) {
    const Mod* m = app.current_mod();
    if (m == nullptr || app.candidate.item != app.selected)
        return;
    const std::string ext = item.kind == Kind::sound ? lower(u8(app.candidate.staged.extension())) : ".png";
    const fs::path destination = m->dir / path_u8(replacement_path(item, ext, app.candidate.any_palette));
    const fs::path staged = app.candidate.staged;
    int ow = 0, oh = 0;
    std::string ignored;
    if (item.kind == Kind::texture || item.kind == Kind::sheet)
        png_size(item.original, &ow, &oh, &ignored);

    const WriteResult r = write_into_mod(
        m->dir, destination, [&](const fs::path& temporary) { return copy_bytes(staged, temporary); },
        [&](const fs::path& temporary) -> std::string {
            CheckResult c = item.kind == Kind::texture ? check_texture(temporary, ow, oh)
                            : item.kind == Kind::sheet ? check_sheet(temporary, ow, oh)
                                                       : check_sound(temporary);
            if (c.verdict != Verdict::refused)
                return {};
            return c.findings.empty() ? std::string("refused") : finding_text(app, c.findings.back());
        });
    if (!r.ok) {
        app.tell(r.locked ? tr(app.language, S::save_locked) : fmt(app.language, S::save_failed, {r.why}), false);
        return;
    }
    // A sound of another extension in this mod would come first or stay
    // behind (the port tries .wav, .flac, .ogg): only the new one is kept.
    if (item.kind == Kind::sound) {
        for (const auto& f : files_in_mod(m->dir, item)) {
            std::error_code ec;
            if (!fs::equivalent(f, destination, ec))
                fs::remove(f, ec);
        }
    }
    app.images->forget(destination);
    app.tell(fmt(app.language, S::saved, {u8(destination.lexically_relative(app.settings.root))}) + "  " +
                 tr(app.language, S::restart_note),
             true);
    app.clear_candidate();
}

// A 3D texture's export, as a 2D sheet's: its size, then it alone or every one into a folder.
void draw_texture_export(App& app, const Item& item) {
    ImGui::SetNextItemWidth(70.0f * ImGui::GetStyle().FontScaleDpi);
    if (ImGui::BeginCombo(tr(app.language, S::texture_export_size),
                          (std::to_string(app.texture_scale) + "x").c_str())) {
        for (int k = 1; k <= 8; ++k) {
            if (ImGui::Selectable((std::to_string(k) + "x").c_str(), app.texture_scale == k))
                app.texture_scale = k;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr(app.language, S::texture_export_size_note));
    same_line_if_room(button_width(tr(app.language, S::texture_export)));
    if (ImGui::Button(tr(app.language, S::texture_export)))
        choose_save_file(app, export_name(item), "PNG", "png", app.dialog_texture_exports);
    same_line_if_room(button_width(tr(app.language, S::textures_export_all)));
    ImGui::BeginDisabled(app.bulk_export != nullptr && !app.bulk_export->done);
    if (ImGui::Button(tr(app.language, S::textures_export_all))) {
        SDL_ShowOpenFolderDialog(
            [](void* user, const char* const* folders, int) {
                if (folders == nullptr || folders[0] == nullptr)
                    return;
                App* a = static_cast<App*>(user);
                std::lock_guard lock(a->dialog_mutex);
                a->dialog_texture_folders.emplace_back(folders[0]);
            },
            &app, app.window, nullptr, false);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tr(app.language, S::textures_export_all_note));
}

}  // namespace

void texture_export_to(App& app, const fs::path& file) {
    if (app.selected < 0 || app.selected >= static_cast<int>(app.catalog.items.size()))
        return;
    const Item& item = app.catalog.items[static_cast<std::size_t>(app.selected)];
    if (item.kind != Kind::texture)
        return;
    fs::path out = file;
    if (lower(u8(out.extension())) != ".png")
        out += ".png";
    std::string why;
    if (!copy_texture(item.original, out, app.texture_scale, &why)) {
        app.tell(fmt(app.language, S::save_failed, {why}), false);
        return;
    }
    app.tell(fmt(app.language, S::sheet_exported, {u8(out)}), true);
}

void textures_export_to(App& app, const fs::path& folder) {
    if (app.bulk_export != nullptr && !app.bulk_export->done)
        return;
    app.bulk_export = std::make_unique<App::BulkExport>();
    App::BulkExport* job = app.bulk_export.get();
    job->textures = true;
    job->folder = folder;
    job->thread = std::thread([job, items = app.catalog.items, scale = app.texture_scale]() {
        std::vector<std::string> failed;
        const int written = export_textures(items, scale, job->folder, bulk_progress(job), &failed);
        bulk_finish(job, written, std::move(failed));
    });
}

void choose_file(App& app, const Item& item) {
    static const SDL_DialogFileFilter pictures[] = {{"PNG, JPEG, TGA, BMP", "png;jpg;jpeg;tga;bmp"}};
    static const SDL_DialogFileFilter sounds[] = {{"WAV, FLAC, Ogg Vorbis", "wav;flac;ogg"}};
    static const SDL_DialogFileFilter models[] = {{"FBX, glTF, glb, OBJ", "fbx;gltf;glb;obj"}};
    const bool picture = item.kind == Kind::texture || item.kind == Kind::sheet;
    SDL_ShowOpenFileDialog(
        [](void* user, const char* const* files, int) {
            if (files == nullptr || files[0] == nullptr)
                return;
            App* a = static_cast<App*>(user);
            std::lock_guard lock(a->dialog_mutex);
            a->dialog_files.emplace_back(files[0]);
        },
        &app, app.window,
        picture                                               ? pictures
        : item.kind == Kind::model || item.kind == Kind::room ? models
                                                              : sounds,
        1, nullptr, false);
}

void draw_item(App& app) {
    // a movie's video plays only while its movie is shown
    if (app.selected < 0 || app.selected >= static_cast<int>(app.catalog.items.size()) ||
        app.catalog.items[static_cast<std::size_t>(app.selected)].kind != Kind::movie) {
        movie_view_leave();
    }
    if (app.selected < 0 || app.selected >= static_cast<int>(app.catalog.items.size())) {
        ImGui::TextDisabled("%s", tr(app.language, S::select_item));
        return;
    }
    const Item& item = app.catalog.items[static_cast<std::size_t>(app.selected)];
    ImGui::TextWrapped("%s", item.key.c_str());

    // which file the game uses now
    const auto held = holders(app.mods_root, item);
    if (item.kind != Kind::movie) {  // a movie's view says it, the player's own HD video included
        ImGui::TextUnformatted(fmt(app.language, S::in_game_now,
                                   {held.empty() ? std::string(tr(app.language, S::from_game)) : held.front().mod})
                                   .c_str());
    }
    if (held.size() > 1) {
        ImGui::TextDisabled("%s", tr(app.language, S::held_by));
        for (const auto& h : held)
            ImGui::BulletText("%s  %s", h.mod.c_str(), u8(h.file.filename()).c_str());
    }
    ImGui::Separator();

    switch (item.kind) {
    case Kind::texture:
    case Kind::sheet: draw_image_view(app, item); break;
    case Kind::sound: draw_sound_view(app, item); break;
    case Kind::model: draw_model_view(app, item); break;
    case Kind::room: draw_map_view(app, item); break;
    case Kind::mission: draw_mission_view(app, item); break;
    case Kind::text_file: draw_text_view(app, item); break;
    case Kind::movie: draw_movie_view(app, item); break;
    }
}

void draw_candidate_controls(App& app, const Item& item) {
    // where a replacement comes from, what the port makes of it, and saving it
    if (app.current_mod() == nullptr) {
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1.0f), "%s", tr(app.language, S::choose_mod_first));
        return;
    }
    ImGui::TextUnformatted(tr(app.language, S::drop_here));
    same_line_if_room(button_width(tr(app.language, S::choose_file)));
    if (ImGui::Button(tr(app.language, S::choose_file)))
        choose_file(app, item);
    if (!app.candidate_error.empty())
        ImGui::TextColored(verdict_colour(Verdict::refused), "%s", app.candidate_error.c_str());
    if (app.candidate.item != app.selected)
        return;

    ImGui::TextDisabled("%s", u8(app.candidate.source).c_str());
    ImGui::TextColored(verdict_colour(app.candidate.check.verdict), "%s",
                       tr(app.language, verdict_word(app.candidate.check.verdict)));
    for (const auto& f : app.candidate.check.findings)
        ImGui::BulletText("%s", finding_text(app, f).c_str());
    if (!any_palette_key(item).empty())
        ImGui::Checkbox(tr(app.language, S::any_palette), &app.candidate.any_palette);

    ImGui::BeginDisabled(app.candidate.check.verdict == Verdict::refused);
    const Mod* m = app.current_mod();
    const auto existing = files_in_mod(m->dir, item);
    if (ImGui::Button(tr(app.language, S::save))) {
        if (existing.empty())
            save(app, item);
        else
            ImGui::OpenPopup("###overwrite");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal((std::string(tr(app.language, S::replace)) + "###overwrite").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(fmt(app.language, S::overwrite_confirm,
                                   {existing.empty() ? std::string() : u8(existing.front().filename())})
                                   .c_str());
        if (ImGui::Button(tr(app.language, S::replace))) {
            save(app, item);
            ImGui::CloseCurrentPopup();
        }
        same_line_if_room(button_width(tr(app.language, S::cancel)));
        if (ImGui::Button(tr(app.language, S::cancel)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void draw_image_view(App& app, const Item& item) {
    if (item.kind == Kind::sheet) {
        if (draw_sheet_mode(app)) {
            draw_sheet_assembled(app, item);
            return;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("%s", tr(app.language, S::sheet_tiles_note));
        ImGui::PopStyleColor();
    }
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const float box_h = std::max(160.0f, ImGui::GetContentRegionAvail().y * 0.55f);
    const auto held = holders(app.mods_root, item);
    fs::path replacement;
    if (app.candidate.item == app.selected && app.candidate.check.verdict != Verdict::refused &&
        !app.candidate.staged.empty()) {
        replacement = app.candidate.staged;
    } else if (!held.empty()) {
        replacement = held.front().file;
    }

    if (ImGui::BeginTable("##pair", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(tr(app.language, S::original));
        draw_picture(app, item.original, ImVec2(half, box_h));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(tr(app.language, S::replacement));
        if (!replacement.empty())
            draw_picture(app, replacement, ImVec2(half, box_h));
        else
            ImGui::Dummy(ImVec2(half, box_h + ImGui::GetTextLineHeightWithSpacing()));
        ImGui::EndTable();
    }
    if (item.kind == Kind::texture)
        draw_texture_export(app, item);
    ImGui::Separator();
    draw_candidate_controls(app, item);
}

}  // namespace studio
