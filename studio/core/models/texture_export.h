// The game's 3D textures (the models' pictures, as the port's graphics dump writes them) written out to
// paint over, as the 2D sheets are: each at 1 to 8 times its size, each pixel repeated.
#pragma once

#include "core/files/catalog.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace studio {

// A dumped texture `picture` written as `out`, `scale` times as wide and tall with each texel repeated
// (the game's pixels kept, to paint over at that size: the port takes a larger picture with the same
// UVs); at 1 the file itself. False with the reason (never written over the dump's own picture).
bool copy_texture(const std::filesystem::path& picture, const std::filesystem::path& out, int scale, std::string* why);

// The catalog's textures (`items`) into `folder` as `<key>.png`, where a mod's replacement goes under
// graphics/, at `scale`. `each` hears the progress (done, total, the key just written or empty); false
// stops. How many were written; those that could not be, with why, go to `failed`.
int export_textures(const std::vector<Item>& items, int scale, const std::filesystem::path& folder,
                    const std::function<bool(std::size_t, std::size_t, const std::string&)>& each,
                    std::vector<std::string>* failed);

}  // namespace studio
