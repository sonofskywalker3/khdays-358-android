#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "khdays/assets/mesh.h"
#include "khdays/assets/texture.h"

namespace khdays::assets {

// One joint of an imported skin: the palette entry it fills, its bone name (for
// matching a DS skeleton), and its inverse-bind matrix. Used to retarget DS
// animations onto the imported geometry.
struct ImportedJoint final {
    std::uint32_t palette_index = 0U;
    std::string name;
    std::array<float, 16> inverse_bind{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    // The joint's global transform as the file places it (its rest),
    // column-major, and its parent: the nearest ancestor that is a joint of
    // the same skin, as an index into ImportedModel::joints, or -1.
    std::array<float, 16> world{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    int parent = -1;
    int node = -1;  // its node, an index into ImportedModel::nodes
};

// A node of the file at rest under its parent: what a model's own animations
// (a room's moving pieces) move.
struct ImportedNode final {
    std::string name;
    int parent = -1;  // an index into ImportedModel::nodes
    std::array<float, 3> translation{0.0F, 0.0F, 0.0F};
    std::array<float, 4> rotation{0.0F, 0.0F, 0.0F, 1.0F};  // quaternion x y z w
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
    std::array<float, 16> local{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
};

// One of a model's own animations (glTF's): keys of its nodes' translations,
// rotations (quaternions x y z w) and scales; looped when the file says so
// (the port's extras khdays_loop and khdays_loop_from).
struct ImportedTrack final {
    enum class Path { translation, rotation, scale };
    int node = 0;
    Path path = Path::translation;
    bool step = false;          // held to the next key, else linear
    std::vector<float> times;   // seconds
    std::vector<float> values;  // 3 per key, 4 for rotations
};

struct ImportedAnimation final {
    std::string name;
    float duration = 0.0F;
    bool loop = false;
    float loop_from = 0.0F;
    std::vector<ImportedTrack> tracks;
};

// A model imported from a mod's file (glTF, FBX) in glTF's frame (Y up, metres), with the textures it
// ships with: each skin a block of palette matrices (jointGlobal * inverseBind) and its vertices up to
// four bones, so it renders in its authored pose and can be re-posed; static meshes baked. Texture
// coordinates in texels, as the DS's.
struct ImportedModel final {
    NeutralModel model;
    std::vector<std::pair<std::string, DecodedTexture>> textures;
    // Skin joints across all skins, aligned with the palette blocks built for
    // them. Empty for a fully static model.
    std::vector<ImportedJoint> joints;
    // Textures the model uses that were not found or could not be read (each
    // drawn white), with the reason, for the caller to report.
    std::vector<std::pair<std::string, std::string>> missing_textures;
    // The file's nodes and its own animations of them (a room's moving pieces).
    std::vector<ImportedNode> nodes;
    std::vector<ImportedAnimation> animations;
};

}  // namespace khdays::assets
