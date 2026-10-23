#include "core/models/texture_export.h"

#include "core/files/paths.h"

extern "C" {
#include "render/png_io.h"
}

#include <cstring>

namespace studio {

bool copy_texture(const fs::path& picture, const fs::path& out, int scale, std::string* why) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    const bool same = fs::exists(out, ec) && fs::equivalent(out, picture, ec);
    if (same)
        return scale <= 1 ? true : fail(u8(out) + " is the dump's own picture: export it elsewhere");
    if (scale <= 1) {
        fs::copy_file(picture, out, fs::copy_options::overwrite_existing, ec);
        return ec ? fail("could not write " + u8(out) + ": " + ec.message()) : true;
    }
    int w = 0, h = 0;
    const char* error = nullptr;
    std::uint8_t* px = khdays_png_read(u8(picture).c_str(), &w, &h, &error);
    if (px == nullptr)
        return fail("could not read " + u8(picture) + (error != nullptr ? std::string(": ") + error : ""));
    const std::size_t s = static_cast<std::size_t>(scale), big_w = static_cast<std::size_t>(w) * s,
                      big_h = static_cast<std::size_t>(h) * s;
    std::vector<std::uint8_t> big(big_w * big_h * 4);
    for (std::size_t y = 0; y < big_h; ++y) {
        for (std::size_t x = 0; x < big_w; ++x)
            std::memcpy(&big[(y * big_w + x) * 4], px + ((y / s) * static_cast<std::size_t>(w) + x / s) * 4, 4);
    }
    khdays_png_free(px);
    if (const char* e = khdays_png_write(u8(out).c_str(), big.data(), static_cast<int>(big_w), static_cast<int>(big_h)))
        return fail("could not write " + u8(out) + ": " + e);
    return true;
}

int export_textures(const std::vector<Item>& items, int scale, const fs::path& folder,
                    const std::function<bool(std::size_t, std::size_t, const std::string&)>& each,
                    std::vector<std::string>* failed) {
    std::vector<const Item*> list;
    for (const auto& it : items) {
        if (it.kind == Kind::texture)
            list.push_back(&it);
    }
    int written = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!each(i, list.size(), std::string()))
            return written;
        std::string why;
        if (copy_texture(list[i]->original, folder / path_u8(list[i]->key + ".png"), scale, &why)) {
            ++written;
            if (!each(i + 1, list.size(), list[i]->key))
                return written;
        } else if (failed != nullptr) {
            failed->push_back(list[i]->key + ": " + why);
        }
    }
    each(list.size(), list.size(), std::string());
    return written;
}

}  // namespace studio
