#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "khdays/assets/texture.h"  // DecodedTexture

namespace khdays::assets {

// A DDS picture decoded to top-down RGBA: the two kinds the KH2 remaster's map
// pictures use, BC3 (four-character code DXT5) and plain 32-bit (code 0, the
// channels by the header's masks). Any other kind throws std::runtime_error
// naming its code.
DecodedTexture decode_dds(const std::uint8_t* data, std::size_t size, const std::string& name);
DecodedTexture read_dds(const std::filesystem::path& path);

}  // namespace khdays::assets
