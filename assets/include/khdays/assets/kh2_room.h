#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "khdays/assets/gltf_export.h"  // Scene
#include "khdays/assets/texture.h"      // DecodedTexture

namespace khdays::assets::kh2 {

// A KH2 map made into a scene for glTF, in KH2's units and axes (Days's room
// is the same axes at 0.01 of the size): the background, the skies and each
// placed piece with its motion; textures from the remastered pictures, their
// used region, scroll and frames.
struct Room final {
    Scene scene;
    std::vector<std::string> report;  // what was left out or is not played, for the player
};

// A picture the scene names, handed over as it is made (file name beside the
// .gltf, picture) so the caller writes it without all of them in memory.
using PictureSink = std::function<void(const std::string& file, const DecodedTexture& picture)>;

// `data` a .map; `hd_folder` its remastered pictures (-N.dds); `name` the map's
// (tt02), which begins its pictures' file names. Throws std::runtime_error
// when the map cannot be read.
Room room_model(const std::uint8_t* data, std::size_t size, const std::filesystem::path& hd_folder,
                const std::string& name, const PictureSink& write_picture);

// The map's own floors and walls (its ID_ collision) as a scene in the map's units and axes, like its
// model: one mesh drawn from both sides, for a room change to make the room's collision from. Throws
// std::runtime_error when the map has none.
Scene collision_scene(const std::uint8_t* data, std::size_t size, const std::string& name);

}  // namespace khdays::assets::kh2
