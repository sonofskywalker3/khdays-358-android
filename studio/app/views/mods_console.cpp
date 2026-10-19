// The mods' console (app.h): the running game's mods' log as it grows -- prints, errors with their
// file and line, each mod's state -- and reloading a mod's scripts in the running game.
#include "../app.h"
#include "../strings.h"

#include "core/mods/mods_log.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <memory>
#include <string>

namespace studio {

namespace {

struct Console {
    std::unique_ptr<ModsLog> log;
    std::filesystem::path root;
    std::uint64_t polled = 0;
    std::string mod;  // the mod shown, empty: all
    bool errors_only = false;
    bool follow = true;  // kept at the newest line
};

Console console;

ImVec4 kind_colour(const std::string& kind) {
    if (kind == "error" || kind == "stopped" || kind == "refused")
        return ImVec4(0.95f, 0.45f, 0.40f, 1.0f);
    if (kind == "paused")
        return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
    if (kind == "loaded" || kind == "reloaded" || kind == "resumed")
        return ImVec4(0.55f, 0.85f, 0.55f, 1.0f);
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

void reload(App& app, const std::string& mod) {
    std::string why;
    if (ask_mod_reload(app.settings.root / "saves", mod, &why)) {
        char said[512];
        SDL_snprintf(said, sizeof(said), tr(app.language, S::console_reload_asked),
                     mod == "*" ? tr(app.language, S::console_all_mods) : mod.c_str());
        app.tell(said, true);
    } else {
        app.tell(why, false);
    }
}

}  // namespace

void draw_mods_console(App& app, bool* open) {
    const float k = ImGui::GetStyle().FontScaleDpi;
    const std::uint64_t now = SDL_GetTicks();
    if (!console.log || console.root != app.settings.root) {
        console.root = app.settings.root;
        console.log = std::make_unique<ModsLog>(app.settings.root / "saves" / "logs" / "mods.log");
    }
    if (now - console.polled >= 250) {
        console.polled = now;
        console.log->poll();
    }
    ImGui::SetNextWindowSize(ImVec2(760.0f * k, 420.0f * k), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr(app.language, S::console_title), open)) {
        ImGui::End();
        return;
    }
    // the mods seen, their states, and their reloads
    ImGui::SetNextItemWidth(220.0f * k);
    if (ImGui::BeginCombo("##mod", console.mod.empty() ? tr(app.language, S::console_all_mods) : console.mod.c_str())) {
        if (ImGui::Selectable(tr(app.language, S::console_all_mods), console.mod.empty()))
            console.mod.clear();
        for (const auto& [mod, state] : console.log->states()) {
            const std::string label = mod + "  (" + state + ")";
            if (ImGui::Selectable(label.c_str(), console.mod == mod))
                console.mod = mod;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox(tr(app.language, S::console_errors_only), &console.errors_only);
    ImGui::SameLine();
    if (!console.mod.empty() && ImGui::Button(tr(app.language, S::console_reload)))
        reload(app, console.mod);
    if (!console.mod.empty())
        ImGui::SameLine();
    if (ImGui::Button(tr(app.language, S::console_reload_all)))
        reload(app, "*");
    if (console.log->last_growth() == 0) {
        ImGui::TextDisabled("%s", tr(app.language, S::console_no_log));
    } else if (now - console.log->last_growth() > 10000) {
        ImGui::TextDisabled(tr(app.language, S::console_quiet),
                            static_cast<int>((now - console.log->last_growth()) / 1000));
    }
    ImGui::Separator();

    ImGui::BeginChild("##lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    for (const ModLogLine& l : console.log->lines()) {
        if (!console.mod.empty() && l.mod != console.mod)
            continue;
        if (console.errors_only && l.kind != "error" && l.kind != "stopped" && l.kind != "refused" &&
            l.kind != "paused")
            continue;
        ImGui::TextDisabled("%9.3f", l.seconds);
        ImGui::SameLine();
        ImGui::TextUnformatted(l.mod.c_str());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kind_colour(l.kind));
        ImGui::TextUnformatted(l.kind.c_str());
        if (l.where != "-") {
            ImGui::SameLine();
            ImGui::TextUnformatted(l.where.c_str());
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(l.text.c_str());
    }
    // kept at the newest line while it is in sight
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace studio
