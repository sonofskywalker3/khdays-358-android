// The port's mods' log and the reloads asked of the game (mods_log.h).
#include "core/mods/mods_log.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

namespace studio {

bool parse_mod_log_line(const std::string& line, ModLogLine* out) {
    // four words, then the text (which may be empty)
    std::string words[4];
    std::size_t at = 0;
    for (std::string& w : words) {
        const std::size_t end = line.find(' ', at);
        w = line.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (w.empty())
            return false;
        at = end == std::string::npos ? line.size() : end + 1;
    }
    char* end = nullptr;
    out->seconds = std::strtod(words[0].c_str(), &end);
    if (end == words[0].c_str() || *end != '\0')
        return false;
    out->mod = words[1];
    out->kind = words[2];
    out->where = words[3];
    out->text = at < line.size() ? line.substr(at) : std::string();
    return true;
}

bool ModsLog::poll() {
    std::error_code e;
    const auto size = fs::file_size(file_, e);
    if (e)
        return false;
    if (size < read_) {
        // a new run's log (the game keeps the last one as mods.prev.log): read from its start
        read_ = 0;
        partial_.clear();
        lines_.clear();
        states_.clear();
    }
    if (size == read_)
        return false;
    std::ifstream in(file_, std::ios::binary);
    if (!in)
        return false;
    in.seekg(static_cast<std::streamoff>(read_));
    std::string added(static_cast<std::size_t>(size - read_), '\0');
    in.read(added.data(), static_cast<std::streamsize>(added.size()));
    added.resize(static_cast<std::size_t>(in.gcount()));
    read_ += added.size();
    partial_ += added;
    bool any = false;
    for (std::size_t eol; (eol = partial_.find('\n')) != std::string::npos;) {
        std::string line = partial_.substr(0, eol);
        partial_.erase(0, eol + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        ModLogLine l;
        if (!parse_mod_log_line(line, &l))
            continue;
        if (l.kind == "loaded" || l.kind == "refused" || l.kind == "stopped" || l.kind == "paused" ||
            l.kind == "resumed" || l.kind == "reloaded")
            states_[l.mod] = l.kind;
        lines_.push_back(std::move(l));
        any = true;
    }
    if (any)
        last_growth_ = SDL_GetTicks();
    return any;
}

bool ask_mod_reload(const fs::path& saves, const std::string& mod, std::string* why) {
    std::error_code e;
    const fs::path file = saves / "mods" / "reload.txt";
    fs::create_directories(file.parent_path(), e);
    std::ofstream out(file, std::ios::binary | std::ios::app);
    out << mod << '\n';
    out.flush();
    if (!out) {
        *why = "cannot write " + file.string();
        return false;
    }
    return true;
}

}  // namespace studio
