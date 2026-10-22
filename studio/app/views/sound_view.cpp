// Sounds: the original, the replacement in the game now and a
// candidate, each played as the port decodes it (port/sound/replace.c),
// and the candidate saved under the sound's name.
#include "../app.h"

#include "core/files/paths.h"

#include "imgui.h"

extern "C" {
#include "replace.h"
}

#include <map>

namespace studio {

namespace {

// One sound playing at a time.
struct Player {
    SDL_AudioStream* stream = nullptr;
    std::string what;    // the file playing
    Uint64 ends_at = 0;  // when it has been heard to its end (the device still plays what it took)
} player;

void play(App& app, const fs::path& file) {
    stop_sounds();
    KhdaysReplacement sound{};
    char why[256] = {};
    if (!khdays_replacement_load(&sound, u8(file).c_str(), nullptr, why, static_cast<int>(sizeof(why)))) {
        app.tell(fmt(app.language, S::sound_cannot_play, {u8(file.filename()), why}), false);
        return;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = sound.channels;
    spec.freq = static_cast<int>(sound.rate);
    player.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (player.stream == nullptr) {
        app.tell(fmt(app.language, S::sound_cannot_play, {u8(file.filename()), SDL_GetError()}), false);
    } else {
        SDL_PutAudioStreamData(player.stream, sound.samples,
                               static_cast<int>(sound.frames * static_cast<uint64_t>(sound.channels) * 2u));
        SDL_FlushAudioStream(player.stream);
        SDL_ResumeAudioStreamDevice(player.stream);
        player.what = u8(file);
        player.ends_at = SDL_GetTicks() + sound.frames * 1000u / sound.rate + 250u;
    }
    khdays_replacement_free(&sound);
}

bool playing(const fs::path& file) {
    if (player.stream == nullptr || player.what != u8(file))
        return false;
    if (SDL_GetAudioStreamQueued(player.stream) <= 0 && SDL_GetTicks() >= player.ends_at) {
        stop_sounds();
        return false;
    }
    return true;
}

// The original's description, decoded once.
const CheckResult& describe(const fs::path& file) {
    static std::map<std::string, CheckResult> known;
    const std::string key = u8(file);
    auto it = known.find(key);
    if (it == known.end())
        it = known.emplace(key, check_sound(file)).first;
    return it->second;
}

void sound_row(App& app, const char* id, const fs::path& file) {
    ImGui::PushID(id);
    if (playing(file)) {
        if (ImGui::Button(tr(app.language, S::stop)))
            stop_sounds();
    } else if (ImGui::Button(tr(app.language, S::play))) {
        play(app, file);
    }
    ImGui::PopID();
}

}  // namespace

void stop_sounds() {
    if (player.stream != nullptr) {
        SDL_DestroyAudioStream(player.stream);
        player.stream = nullptr;
    }
    player.what.clear();
}

void draw_sound_view(App& app, const Item& item) {
    ImGui::TextUnformatted(tr(app.language, S::original));
    sound_row(app, "original", item.original);
    for (const auto& f : describe(item.original).findings) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", finding_text(app, f).c_str());
        ImGui::PopStyleColor();
    }

    const auto held = holders(app.mods_root, item);
    if (!held.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(fmt(app.language, S::in_game_now, {held.front().mod}).c_str());
        sound_row(app, "held", held.front().file);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", u8(held.front().file.filename()).c_str());
    }

    ImGui::Separator();
    if (app.candidate.item == app.selected && app.candidate.check.verdict != Verdict::refused &&
        !app.candidate.staged.empty()) {
        ImGui::TextUnformatted(tr(app.language, S::replacement));
        sound_row(app, "candidate", app.candidate.staged);
    }
    draw_candidate_controls(app, item);
}

}  // namespace studio
