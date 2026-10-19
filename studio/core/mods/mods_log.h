// The port's mods' log (saves/logs/mods.log, port/runtime/scripts/mods_log.h), followed as the game
// writes it, and the reloads the tool asks of the game (saves/mods/reload.txt): the console for
// modders.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace studio {

struct ModLogLine {
    double seconds = 0;                  // since the game started its log
    std::string mod, kind, where, text;  // kind: loaded, refused, print, error, stopped, paused, resumed, reloaded
};

// One line of the log, "<seconds> <mod> <kind> <where> <text>"; false when it is not one.
bool parse_mod_log_line(const std::string& line, ModLogLine* out);

class ModsLog {
public:
    explicit ModsLog(std::filesystem::path file) : file_(std::move(file)) {}

    // Reads what the game added since the last look (the whole file again when it is a new run's).
    // Returns whether there are new lines.
    bool poll();

    const std::vector<ModLogLine>& lines() const {
        return lines_;
    }
    // Each mod's last state line's kind (loaded, refused, stopped, paused, resumed, reloaded), by mod.
    const std::map<std::string, std::string>& states() const {
        return states_;
    }
    // When the log last grew (SDL_GetTicks), 0 never.
    std::uint64_t last_growth() const {
        return last_growth_;
    }

private:
    std::filesystem::path file_;
    std::uintmax_t read_ = 0;
    std::filesystem::file_time_type started_{};
    std::string partial_;
    std::vector<ModLogLine> lines_;
    std::map<std::string, std::string> states_;
    std::uint64_t last_growth_ = 0;
};

// Asks the running game to load `mod` (its id; "*" every mod) again: a line in saves/mods/reload.txt,
// which the game reads within a second. False with the reason in `why`.
bool ask_mod_reload(const std::filesystem::path& saves, const std::string& mod, std::string* why);

}  // namespace studio
